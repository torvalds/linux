// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *    copyright		   : (C) 1999 Axel Dziemba (axel.dziemba@ines.de)
 *			    (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#include "ines.h"

#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/bitops.h>
#include <asm/dma.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "gpib_pci_ids.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver for Ines iGPIB 72010");

int ines_line_status(const gpib_board_t *board)
{
	int status = ValidALL;
	int bcm_bits;
	struct ines_priv *ines_priv;
	struct nec7210_priv *nec_priv;

	ines_priv = board->private_data;
	nec_priv = &ines_priv->nec7210_priv;

	bcm_bits = ines_inb(ines_priv, BUS_CONTROL_MONITOR);

	if (bcm_bits & BCM_REN_BIT)
		status |= BusREN;
	if (bcm_bits & BCM_IFC_BIT)
		status |= BusIFC;
	if (bcm_bits & BCM_SRQ_BIT)
		status |= BusSRQ;
	if (bcm_bits & BCM_EOI_BIT)
		status |= BusEOI;
	if (bcm_bits & BCM_NRFD_BIT)
		status |= BusNRFD;
	if (bcm_bits & BCM_NDAC_BIT)
		status |= BusNDAC;
	if (bcm_bits & BCM_DAV_BIT)
		status |= BusDAV;
	if (bcm_bits & BCM_ATN_BIT)
		status |= BusATN;

	return status;
}

void ines_set_xfer_counter(struct ines_priv *priv, unsigned int count)
{
	if (count > 0xffff) {
		pr_err("ines: bug! tried to set xfer counter > 0xffff\n");
		return;
	}
	ines_outb(priv, (count >> 8) & 0xff, XFER_COUNT_UPPER);
	ines_outb(priv, count & 0xff, XFER_COUNT_LOWER);
}

unsigned int ines_t1_delay(gpib_board_t *board, unsigned int nano_sec)
{
	struct ines_priv *ines_priv = board->private_data;
	struct nec7210_priv *nec_priv = &ines_priv->nec7210_priv;
	unsigned int retval;

	retval = nec7210_t1_delay(board, nec_priv, nano_sec);

	if (nano_sec <= 250) {
		write_byte(nec_priv, INES_AUXD | INES_FOLLOWING_T1_250ns |
			   INES_INITIAL_T1_2000ns, AUXMR);
		retval = 250;
	} else if (nano_sec <= 350) {
		write_byte(nec_priv, INES_AUXD | INES_FOLLOWING_T1_350ns |
			   INES_INITIAL_T1_2000ns, AUXMR);
		retval = 350;
	} else {
		write_byte(nec_priv, INES_AUXD | INES_FOLLOWING_T1_500ns |
			   INES_INITIAL_T1_2000ns, AUXMR);
		retval = 500;
	}

	return retval;
}

static inline unsigned short num_in_fifo_bytes(struct ines_priv *ines_priv)
{
	return ines_inb(ines_priv, IN_FIFO_COUNT);
}

static ssize_t pio_read(gpib_board_t *board, struct ines_priv *ines_priv, uint8_t *buffer,
			size_t length, size_t *nbytes)
{
	ssize_t retval = 0;
	unsigned int num_fifo_bytes, i;
	struct nec7210_priv *nec_priv = &ines_priv->nec7210_priv;

	*nbytes = 0;
	while (*nbytes < length) {
		if (wait_event_interruptible(board->wait,
					     num_in_fifo_bytes(ines_priv) ||
					     test_bit(RECEIVED_END_BN, &nec_priv->state) ||
					     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
					     test_bit(TIMO_NUM, &board->status))) {
			pr_warn("gpib: pio read wait interrupted\n");
			return -ERESTARTSYS;
		}
		if (test_bit(TIMO_NUM, &board->status))
			return -ETIMEDOUT;
		if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
			return -EINTR;

		num_fifo_bytes = num_in_fifo_bytes(ines_priv);
		if (num_fifo_bytes + *nbytes > length)	{
			pr_warn("ines: counter allowed %li extra byte(s)\n",
				(long)(num_fifo_bytes - (length - *nbytes)));
			num_fifo_bytes = length - *nbytes;
		}
		for (i = 0; i < num_fifo_bytes; i++)
			buffer[(*nbytes)++] = read_byte(nec_priv, DIR);
		if (test_bit(RECEIVED_END_BN, &nec_priv->state) &&
		    num_in_fifo_bytes(ines_priv) == 0)
			break;
		if (need_resched())
			schedule();
	}
	/* make sure RECEIVED_END is in sync */
	ines_interrupt(board);
	return retval;
}

int ines_accel_read(gpib_board_t *board, uint8_t *buffer,
		    size_t length, int *end, size_t *bytes_read)
{
	ssize_t retval = 0;
	struct ines_priv *ines_priv = board->private_data;
	struct nec7210_priv *nec_priv = &ines_priv->nec7210_priv;
	int counter_setting;

	*end = 0;
	*bytes_read = 0;
	if (length == 0)
		return 0;

	clear_bit(DEV_CLEAR_BN, &nec_priv->state);

	write_byte(nec_priv, INES_RFD_HLD_IMMEDIATE, AUXMR);

	//clear in fifo
	nec7210_set_reg_bits(nec_priv, ADMR, IN_FIFO_ENABLE_BIT, 0);
	nec7210_set_reg_bits(nec_priv, ADMR, IN_FIFO_ENABLE_BIT, IN_FIFO_ENABLE_BIT);

	ines_priv->extend_mode_bits |= LAST_BYTE_HANDLING_BIT;
	ines_priv->extend_mode_bits &= ~XFER_COUNTER_OUTPUT_BIT & ~XFER_COUNTER_ENABLE_BIT;
	ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);

	counter_setting = length - num_in_fifo_bytes(ines_priv);
	if (counter_setting > 0) {
		ines_set_xfer_counter(ines_priv, length);
		ines_priv->extend_mode_bits |= XFER_COUNTER_ENABLE_BIT;
		ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);

		// holdoff on END
		nec7210_set_handshake_mode(board, nec_priv, HR_HLDE);
		/* release rfd holdoff */
		write_byte(nec_priv, AUX_FH, AUXMR);
	}

	retval = pio_read(board, ines_priv, buffer, length, bytes_read);
	ines_priv->extend_mode_bits &= ~XFER_COUNTER_ENABLE_BIT;
	ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);
	if (retval < 0)	{
		write_byte(nec_priv, INES_RFD_HLD_IMMEDIATE, AUXMR);
		return retval;
	}
	if (test_and_clear_bit(RECEIVED_END_BN, &nec_priv->state))
		*end = 1;

	return retval;
}

static const int out_fifo_size = 0xff;

static inline unsigned short num_out_fifo_bytes(struct ines_priv *ines_priv)
{
	return ines_inb(ines_priv, OUT_FIFO_COUNT);
}

static int ines_write_wait(gpib_board_t *board, struct ines_priv *ines_priv,
			   unsigned int fifo_threshold)
{
	struct nec7210_priv *nec_priv = &ines_priv->nec7210_priv;

	// wait until byte is ready to be sent
	if (wait_event_interruptible(board->wait,
				     num_out_fifo_bytes(ines_priv) < fifo_threshold ||
				     test_bit(BUS_ERROR_BN, &nec_priv->state) ||
				     test_bit(DEV_CLEAR_BN, &nec_priv->state) ||
				     test_bit(TIMO_NUM, &board->status))) {
		dev_dbg(board->gpib_dev, "gpib write interrupted\n");
		return -ERESTARTSYS;
	}
	if (test_bit(BUS_ERROR_BN, &nec_priv->state))
		return -EIO;
	if (test_bit(DEV_CLEAR_BN, &nec_priv->state))
		return -EINTR;
	if (test_bit(TIMO_NUM, &board->status))
		return -ETIMEDOUT;

	return 0;
}

int ines_accel_write(gpib_board_t *board, uint8_t *buffer, size_t length,
		     int send_eoi, size_t *bytes_written)
{
	size_t count = 0;
	ssize_t retval = 0;
	struct ines_priv *ines_priv = board->private_data;
	struct nec7210_priv *nec_priv = &ines_priv->nec7210_priv;
	unsigned int num_bytes, i;

	*bytes_written = 0;
	//clear out fifo
	nec7210_set_reg_bits(nec_priv, ADMR, OUT_FIFO_ENABLE_BIT, 0);
	nec7210_set_reg_bits(nec_priv, ADMR, OUT_FIFO_ENABLE_BIT, OUT_FIFO_ENABLE_BIT);

	ines_priv->extend_mode_bits |= XFER_COUNTER_OUTPUT_BIT;
	ines_priv->extend_mode_bits &= ~XFER_COUNTER_ENABLE_BIT;
	ines_priv->extend_mode_bits &= ~LAST_BYTE_HANDLING_BIT;
	ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);

	ines_set_xfer_counter(ines_priv, length);
	if (send_eoi)
		ines_priv->extend_mode_bits |= LAST_BYTE_HANDLING_BIT;
	ines_priv->extend_mode_bits |= XFER_COUNTER_ENABLE_BIT;
	ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);

	while (count < length) {
		retval = ines_write_wait(board, ines_priv, out_fifo_size);
		if (retval < 0)
			break;

		num_bytes = out_fifo_size - num_out_fifo_bytes(ines_priv);
		if (num_bytes + count > length)
			num_bytes = length - count;
		for (i = 0; i < num_bytes; i++)
			write_byte(nec_priv, buffer[count++], CDOR);
	}
	if (retval < 0)	{
		ines_priv->extend_mode_bits &= ~XFER_COUNTER_ENABLE_BIT;
		ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);
		*bytes_written = length - num_out_fifo_bytes(ines_priv);
		return retval;
	}
	// wait last byte has been sent
	retval = ines_write_wait(board, ines_priv, 1);
	ines_priv->extend_mode_bits &= ~XFER_COUNTER_ENABLE_BIT;
	ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);
	*bytes_written = length - num_out_fifo_bytes(ines_priv);

	return retval;
}

irqreturn_t ines_pci_interrupt(int irq, void *arg)
{
	gpib_board_t *board = arg;
	struct ines_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;

	if (priv->pci_chip_type == PCI_CHIP_QUANCOM) {
		if ((inb(nec_priv->iobase +
			 QUANCOM_IRQ_CONTROL_STATUS_REG) &
		     QUANCOM_IRQ_ASSERTED_BIT))
			outb(QUANCOM_IRQ_ENABLE_BIT, nec_priv->iobase +
			     QUANCOM_IRQ_CONTROL_STATUS_REG);
	}

	return ines_interrupt(board);
}

irqreturn_t ines_interrupt(gpib_board_t *board)
{
	struct ines_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;
	unsigned int isr3_bits, isr4_bits;
	unsigned long flags;
	int wake = 0;

	spin_lock_irqsave(&board->spinlock, flags);

	nec7210_interrupt(board, nec_priv);
	isr3_bits = ines_inb(priv, ISR3);
	isr4_bits = ines_inb(priv, ISR4);
	if (isr3_bits & IFC_ACTIVE_BIT)	{
		push_gpib_event(board, EventIFC);
		wake++;
	}
	if (isr3_bits & FIFO_ERROR_BIT)
		pr_err("ines gpib: fifo error\n");
	if (isr3_bits & XFER_COUNT_BIT)
		wake++;

	if (isr4_bits & (IN_FIFO_WATERMARK_BIT | IN_FIFO_FULL_BIT | OUT_FIFO_WATERMARK_BIT |
			 OUT_FIFO_EMPTY_BIT))
		wake++;

	if (wake)
		wake_up_interruptible(&board->wait);
	spin_unlock_irqrestore(&board->spinlock, flags);
	return IRQ_HANDLED;
}

static int ines_pci_attach(gpib_board_t *board, const gpib_board_config_t *config);
static int ines_pci_accel_attach(gpib_board_t *board, const gpib_board_config_t *config);
static int ines_isa_attach(gpib_board_t *board, const gpib_board_config_t *config);

static void ines_pci_detach(gpib_board_t *board);
static void ines_isa_detach(gpib_board_t *board);

enum ines_pci_vendor_ids {
	PCI_VENDOR_ID_INES_QUICKLOGIC = 0x16da
};

enum ines_pci_device_ids {
	PCI_DEVICE_ID_INES_GPIB_AMCC = 0x8507,
	PCI_DEVICE_ID_INES_GPIB_QL5030 = 0x11,
};

enum ines_pci_subdevice_ids {
	PCI_SUBDEVICE_ID_INES_GPIB = 0x1072
};

static struct pci_device_id ines_pci_table[] = {
	{PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050, PCI_VENDOR_ID_PLX,
	 PCI_SUBDEVICE_ID_INES_GPIB, 0, 0, 0},
	{PCI_VENDOR_ID_AMCC, PCI_DEVICE_ID_INES_GPIB_AMCC, PCI_VENDOR_ID_AMCC,
	 PCI_SUBDEVICE_ID_INES_GPIB, 0, 0, 0},
	{PCI_VENDOR_ID_INES_QUICKLOGIC, PCI_DEVICE_ID_INES_GPIB_QL5030,
	 PCI_VENDOR_ID_INES_QUICKLOGIC, PCI_DEVICE_ID_INES_GPIB_QL5030, 0, 0, 0},
	{PCI_DEVICE(PCI_VENDOR_ID_QUANCOM, PCI_DEVICE_ID_QUANCOM_GPIB)},
	{0}
};
MODULE_DEVICE_TABLE(pci, ines_pci_table);

struct ines_pci_id {
	unsigned int vendor_id;
	unsigned int device_id;
	int subsystem_vendor_id;
	int subsystem_device_id;
	unsigned int gpib_region;
	unsigned int io_offset;
	enum ines_pci_chip pci_chip_type;
};

static struct ines_pci_id pci_ids[] = {
	{.vendor_id = PCI_VENDOR_ID_PLX,
	 .device_id = PCI_DEVICE_ID_PLX_9050,
	 .subsystem_vendor_id = PCI_VENDOR_ID_PLX,
	 .subsystem_device_id = PCI_SUBDEVICE_ID_INES_GPIB,
	 .gpib_region = 2,
	 .io_offset = 1,
	 .pci_chip_type = PCI_CHIP_PLX9050,
	},
	{.vendor_id = PCI_VENDOR_ID_AMCC,
	 .device_id = PCI_DEVICE_ID_INES_GPIB_AMCC,
	 .subsystem_vendor_id = PCI_VENDOR_ID_AMCC,
	 .subsystem_device_id = PCI_SUBDEVICE_ID_INES_GPIB,
	 .gpib_region = 1,
	 .io_offset = 1,
	 .pci_chip_type = PCI_CHIP_AMCC5920,
	},
	{.vendor_id = PCI_VENDOR_ID_INES_QUICKLOGIC,
	 .device_id = PCI_DEVICE_ID_INES_GPIB_QL5030,
	 .subsystem_vendor_id = PCI_VENDOR_ID_INES_QUICKLOGIC,
	 .subsystem_device_id = PCI_DEVICE_ID_INES_GPIB_QL5030,
	 .gpib_region = 1,
	 .io_offset = 1,
	 .pci_chip_type = PCI_CHIP_QUICKLOGIC5030,
	},
	{.vendor_id = PCI_VENDOR_ID_QUANCOM,
	 .device_id = PCI_DEVICE_ID_QUANCOM_GPIB,
	 .subsystem_vendor_id = -1,
	 .subsystem_device_id = -1,
	 .gpib_region = 0,
	 .io_offset = 4,
	 .pci_chip_type = PCI_CHIP_QUANCOM,
	},
};

static const int num_pci_chips = ARRAY_SIZE(pci_ids);

// wrappers for interface functions
int ines_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end, size_t *bytes_read)
{
	struct ines_priv *priv = board->private_data;
	struct nec7210_priv *nec_priv = &priv->nec7210_priv;
	ssize_t retval;
	int dummy;

	retval = nec7210_read(board, &priv->nec7210_priv, buffer, length, end, bytes_read);
	if (retval < 0)	{
		write_byte(nec_priv, INES_RFD_HLD_IMMEDIATE, AUXMR);

		set_bit(RFD_HOLDOFF_BN, &nec_priv->state);

		nec7210_read_data_in(board, nec_priv, &dummy);
	}
	return retval;
}

int ines_write(gpib_board_t *board, uint8_t *buffer, size_t length, int send_eoi,
	       size_t *bytes_written)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_write(board, &priv->nec7210_priv, buffer, length, send_eoi, bytes_written);
}

int ines_command(gpib_board_t *board, uint8_t *buffer, size_t length, size_t *bytes_written)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_command(board, &priv->nec7210_priv, buffer, length, bytes_written);
}

int ines_take_control(gpib_board_t *board, int synchronous)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_take_control(board, &priv->nec7210_priv, synchronous);
}

int ines_go_to_standby(gpib_board_t *board)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_go_to_standby(board, &priv->nec7210_priv);
}

void ines_request_system_control(gpib_board_t *board, int request_control)
{
	struct ines_priv *priv = board->private_data;

	nec7210_request_system_control(board, &priv->nec7210_priv, request_control);
}

void ines_interface_clear(gpib_board_t *board, int assert)
{
	struct ines_priv *priv = board->private_data;

	nec7210_interface_clear(board, &priv->nec7210_priv, assert);
}

void ines_remote_enable(gpib_board_t *board, int enable)
{
	struct ines_priv *priv = board->private_data;

	nec7210_remote_enable(board, &priv->nec7210_priv, enable);
}

int ines_enable_eos(gpib_board_t *board, uint8_t eos_byte, int compare_8_bits)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_enable_eos(board, &priv->nec7210_priv, eos_byte, compare_8_bits);
}

void ines_disable_eos(gpib_board_t *board)
{
	struct ines_priv *priv = board->private_data;

	nec7210_disable_eos(board, &priv->nec7210_priv);
}

unsigned int ines_update_status(gpib_board_t *board, unsigned int clear_mask)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_update_status(board, &priv->nec7210_priv, clear_mask);
}

int ines_primary_address(gpib_board_t *board, unsigned int address)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_primary_address(board, &priv->nec7210_priv, address);
}

int ines_secondary_address(gpib_board_t *board, unsigned int address, int enable)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_secondary_address(board, &priv->nec7210_priv, address, enable);
}

int ines_parallel_poll(gpib_board_t *board, uint8_t *result)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_parallel_poll(board, &priv->nec7210_priv, result);
}

void ines_parallel_poll_configure(gpib_board_t *board, uint8_t config)
{
	struct ines_priv *priv = board->private_data;

	nec7210_parallel_poll_configure(board, &priv->nec7210_priv, config);
}

void ines_parallel_poll_response(gpib_board_t *board, int ist)
{
	struct ines_priv *priv = board->private_data;

	nec7210_parallel_poll_response(board, &priv->nec7210_priv, ist);
}

void ines_serial_poll_response(gpib_board_t *board, uint8_t status)
{
	struct ines_priv *priv = board->private_data;

	nec7210_serial_poll_response(board, &priv->nec7210_priv, status);
}

uint8_t ines_serial_poll_status(gpib_board_t *board)
{
	struct ines_priv *priv = board->private_data;

	return nec7210_serial_poll_status(board, &priv->nec7210_priv);
}

void ines_return_to_local(gpib_board_t *board)
{
	struct ines_priv *priv = board->private_data;

	nec7210_return_to_local(board, &priv->nec7210_priv);
}

static gpib_interface_t ines_pci_unaccel_interface = {
	.name = "ines_pci_unaccel",
	.attach = ines_pci_attach,
	.detach = ines_pci_detach,
	.read = ines_read,
	.write = ines_write,
	.command = ines_command,
	.take_control = ines_take_control,
	.go_to_standby = ines_go_to_standby,
	.request_system_control = ines_request_system_control,
	.interface_clear = ines_interface_clear,
	.remote_enable = ines_remote_enable,
	.enable_eos = ines_enable_eos,
	.disable_eos = ines_disable_eos,
	.parallel_poll = ines_parallel_poll,
	.parallel_poll_configure = ines_parallel_poll_configure,
	.parallel_poll_response = ines_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ines_line_status,
	.update_status = ines_update_status,
	.primary_address = ines_primary_address,
	.secondary_address = ines_secondary_address,
	.serial_poll_response = ines_serial_poll_response,
	.serial_poll_status = ines_serial_poll_status,
	.t1_delay = ines_t1_delay,
	.return_to_local = ines_return_to_local,
};

static gpib_interface_t ines_pci_interface = {
	.name = "ines_pci",
	.attach = ines_pci_accel_attach,
	.detach = ines_pci_detach,
	.read = ines_accel_read,
	.write = ines_accel_write,
	.command = ines_command,
	.take_control = ines_take_control,
	.go_to_standby = ines_go_to_standby,
	.request_system_control = ines_request_system_control,
	.interface_clear = ines_interface_clear,
	.remote_enable = ines_remote_enable,
	.enable_eos = ines_enable_eos,
	.disable_eos = ines_disable_eos,
	.parallel_poll = ines_parallel_poll,
	.parallel_poll_configure = ines_parallel_poll_configure,
	.parallel_poll_response = ines_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ines_line_status,
	.update_status = ines_update_status,
	.primary_address = ines_primary_address,
	.secondary_address = ines_secondary_address,
	.serial_poll_response = ines_serial_poll_response,
	.serial_poll_status = ines_serial_poll_status,
	.t1_delay = ines_t1_delay,
	.return_to_local = ines_return_to_local,
};

static gpib_interface_t ines_pci_accel_interface = {
	.name = "ines_pci_accel",
	.attach = ines_pci_accel_attach,
	.detach = ines_pci_detach,
	.read = ines_accel_read,
	.write = ines_accel_write,
	.command = ines_command,
	.take_control = ines_take_control,
	.go_to_standby = ines_go_to_standby,
	.request_system_control = ines_request_system_control,
	.interface_clear = ines_interface_clear,
	.remote_enable = ines_remote_enable,
	.enable_eos = ines_enable_eos,
	.disable_eos = ines_disable_eos,
	.parallel_poll = ines_parallel_poll,
	.parallel_poll_configure = ines_parallel_poll_configure,
	.parallel_poll_response = ines_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ines_line_status,
	.update_status = ines_update_status,
	.primary_address = ines_primary_address,
	.secondary_address = ines_secondary_address,
	.serial_poll_response = ines_serial_poll_response,
	.serial_poll_status = ines_serial_poll_status,
	.t1_delay = ines_t1_delay,
	.return_to_local = ines_return_to_local,
};

static gpib_interface_t ines_isa_interface = {
	.name = "ines_isa",
	.attach = ines_isa_attach,
	.detach = ines_isa_detach,
	.read = ines_accel_read,
	.write = ines_accel_write,
	.command = ines_command,
	.take_control = ines_take_control,
	.go_to_standby = ines_go_to_standby,
	.request_system_control = ines_request_system_control,
	.interface_clear = ines_interface_clear,
	.remote_enable = ines_remote_enable,
	.enable_eos = ines_enable_eos,
	.disable_eos = ines_disable_eos,
	.parallel_poll = ines_parallel_poll,
	.parallel_poll_configure = ines_parallel_poll_configure,
	.parallel_poll_response = ines_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ines_line_status,
	.update_status = ines_update_status,
	.primary_address = ines_primary_address,
	.secondary_address = ines_secondary_address,
	.serial_poll_response = ines_serial_poll_response,
	.serial_poll_status = ines_serial_poll_status,
	.t1_delay = ines_t1_delay,
	.return_to_local = ines_return_to_local,
};

static int ines_allocate_private(gpib_board_t *board)
{
	struct ines_priv *priv;

	board->private_data = kmalloc(sizeof(struct ines_priv), GFP_KERNEL);
	if (!board->private_data)
		return -1;
	priv = board->private_data;
	memset(priv, 0, sizeof(struct ines_priv));
	init_nec7210_private(&priv->nec7210_priv);
	return 0;
}

void ines_free_private(gpib_board_t *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

int ines_generic_attach(gpib_board_t *board)
{
	struct ines_priv *ines_priv;
	struct nec7210_priv *nec_priv;

	board->status = 0;

	if (ines_allocate_private(board))
		return -ENOMEM;
	ines_priv = board->private_data;
	nec_priv = &ines_priv->nec7210_priv;
	nec_priv->read_byte = nec7210_ioport_read_byte;
	nec_priv->write_byte = nec7210_ioport_write_byte;
	nec_priv->offset = 1;
	nec_priv->type = IGPIB7210;
	ines_priv->pci_chip_type = PCI_CHIP_NONE;

	return 0;
}

void ines_online(struct ines_priv *ines_priv, const gpib_board_t *board, int use_accel)
{
	struct nec7210_priv *nec_priv = &ines_priv->nec7210_priv;

	/* ines doesn't seem to use internal count register */
	write_byte(nec_priv, ICR | 0, AUXMR);

	write_byte(nec_priv, INES_AUX_XMODE, AUXMR);
	write_byte(nec_priv, INES_RFD_HLD_IMMEDIATE, AUXMR);

	set_bit(RFD_HOLDOFF_BN, &nec_priv->state);

	write_byte(nec_priv, INES_AUXD | 0, AUXMR);
	ines_outb(ines_priv, 0, XDMA_CONTROL);
	ines_priv->extend_mode_bits = 0;
	ines_outb(ines_priv, ines_priv->extend_mode_bits, EXTEND_MODE);
	if (use_accel) {
		ines_outb(ines_priv, 0x80, OUT_FIFO_WATERMARK);
		ines_outb(ines_priv, 0x80, IN_FIFO_WATERMARK);
		ines_outb(ines_priv, IFC_ACTIVE_BIT | ATN_ACTIVE_BIT |
			  FIFO_ERROR_BIT | XFER_COUNT_BIT, IMR3);
		ines_outb(ines_priv, IN_FIFO_WATERMARK_BIT | IN_FIFO_FULL_BIT |
			  OUT_FIFO_WATERMARK_BIT | OUT_FIFO_EMPTY_BIT, IMR4);
	} else {
		nec7210_set_reg_bits(nec_priv, ADMR, IN_FIFO_ENABLE_BIT | OUT_FIFO_ENABLE_BIT, 0);
		ines_outb(ines_priv, IFC_ACTIVE_BIT | FIFO_ERROR_BIT, IMR3);
		ines_outb(ines_priv, 0, IMR4);
	}

	nec7210_board_online(nec_priv, board);
	if (use_accel)
		nec7210_set_reg_bits(nec_priv, IMR1, HR_DOIE | HR_DIIE, 0);
}

static int ines_common_pci_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct ines_priv *ines_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = 0;
	int retval;
	struct ines_pci_id found_id;
	unsigned int i;
	struct pci_dev *pdev;

	memset(&found_id, 0, sizeof(found_id));

	retval = ines_generic_attach(board);
	if (retval)
		return retval;

	ines_priv = board->private_data;
	nec_priv = &ines_priv->nec7210_priv;

	// find board
	ines_priv->pci_device = NULL;
	for (i = 0; i < num_pci_chips && !ines_priv->pci_device; i++) {
		pdev = NULL;
		do {
			if (pci_ids[i].subsystem_vendor_id >= 0 &&
			    pci_ids[i].subsystem_device_id >= 0)
				pdev = pci_get_subsys(pci_ids[i].vendor_id, pci_ids[i].device_id,
						      pci_ids[i].subsystem_vendor_id,
						      pci_ids[i].subsystem_device_id, pdev);
			else
				pdev = pci_get_device(pci_ids[i].vendor_id, pci_ids[i].device_id,
						      pdev);
			if (!pdev)
				break;
			if (config->pci_bus >= 0 && config->pci_bus != pdev->bus->number)
				continue;
			if (config->pci_slot >= 0 && config->pci_slot != PCI_SLOT(pdev->devfn))
				continue;
			found_id = pci_ids[i];
			ines_priv->pci_device = pdev;
			break;
		} while (1);
	}
	if (!ines_priv->pci_device) {
		pr_err("gpib: could not find ines PCI board\n");
		return -1;
	}

	if (pci_enable_device(ines_priv->pci_device)) {
		pr_err("error enabling pci device\n");
		return -1;
	}

	if (pci_request_regions(ines_priv->pci_device, "ines-gpib"))
		return -1;
	nec_priv->iobase = pci_resource_start(ines_priv->pci_device,
					      found_id.gpib_region);

	ines_priv->pci_chip_type = found_id.pci_chip_type;
	nec_priv->offset = found_id.io_offset;
	switch (ines_priv->pci_chip_type) {
	case PCI_CHIP_PLX9050:
		ines_priv->plx_iobase = pci_resource_start(ines_priv->pci_device, 1);
		break;
	case PCI_CHIP_AMCC5920:
		ines_priv->amcc_iobase = pci_resource_start(ines_priv->pci_device, 0);
		break;
	case PCI_CHIP_QUANCOM:
		break;
	case PCI_CHIP_QUICKLOGIC5030:
		break;
	default:
		pr_err("gpib: unspecified chip type? (bug)\n");
		nec_priv->iobase = 0;
		pci_release_regions(ines_priv->pci_device);
		return -1;
	}

	nec7210_board_reset(nec_priv, board);
#ifdef QUANCOM_PCI
	if (ines_priv->pci_chip_type == PCI_CHIP_QUANCOM) {
		/* change interrupt polarity */
		nec_priv->auxb_bits |= HR_INV;
		ines_outb(ines_priv, nec_priv->auxb_bits, AUXMR);
	}
#endif
	isr_flags |= IRQF_SHARED;
	if (request_irq(ines_priv->pci_device->irq, ines_pci_interrupt, isr_flags,
			"pci-gpib", board)) {
		pr_err("gpib: can't request IRQ %d\n", ines_priv->pci_device->irq);
		return -1;
	}
	ines_priv->irq = ines_priv->pci_device->irq;

	// enable interrupts on pci chip
	switch (ines_priv->pci_chip_type) {
	case PCI_CHIP_PLX9050:
		outl(PLX9050_LINTR1_EN_BIT | PLX9050_LINTR1_POLARITY_BIT | PLX9050_PCI_INTR_EN_BIT,
		     ines_priv->plx_iobase + PLX9050_INTCSR_REG);
		break;
	case PCI_CHIP_AMCC5920:
	{
		static const int region = 1;
		static const int num_wait_states = 7;
		u32 bits;

		bits = amcc_prefetch_bits(region, PREFETCH_DISABLED);
		bits |= amcc_PTADR_mode_bit(region);
		bits |= amcc_disable_write_fifo_bit(region);
		bits |= amcc_wait_state_bits(region, num_wait_states);
		outl(bits, ines_priv->amcc_iobase + AMCC_PASS_THRU_REG);
		outl(AMCC_ADDON_INTR_ENABLE_BIT, ines_priv->amcc_iobase + AMCC_INTCS_REG);
	}
	break;
	case PCI_CHIP_QUANCOM:
		outb(QUANCOM_IRQ_ENABLE_BIT, nec_priv->iobase +
		     QUANCOM_IRQ_CONTROL_STATUS_REG);
		break;
	case PCI_CHIP_QUICKLOGIC5030:
		break;
	default:
		pr_err("gpib: unspecified chip type? (bug)\n");
		return -1;
	}

	return 0;
}

int ines_pci_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct ines_priv *ines_priv;
	int retval;

	retval = ines_common_pci_attach(board, config);
	if (retval < 0)
		return retval;

	ines_priv = board->private_data;
	ines_online(ines_priv, board, 0);

	return 0;
}

int ines_pci_accel_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct ines_priv *ines_priv;
	int retval;

	retval = ines_common_pci_attach(board, config);
	if (retval < 0)
		return retval;

	ines_priv = board->private_data;
	ines_online(ines_priv, board, 1);

	return 0;
}

static const int ines_isa_iosize = 0x20;

int ines_isa_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct ines_priv *ines_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = 0;
	int retval;

	retval = ines_generic_attach(board);
	if (retval)
		return retval;

	ines_priv = board->private_data;
	nec_priv = &ines_priv->nec7210_priv;

	if (!request_region(config->ibbase, ines_isa_iosize, "ines_gpib")) {
		pr_err("ines_gpib: ioports at 0x%x already in use\n", config->ibbase);
		return -1;
	}
	nec_priv->iobase = config->ibbase;
	nec_priv->offset = 1;
	nec7210_board_reset(nec_priv, board);
	if (request_irq(config->ibirq, ines_pci_interrupt, isr_flags, "ines_gpib", board)) {
		pr_err("ines_gpib: failed to allocate IRQ %d\n", config->ibirq);
		return -1;
	}
	ines_priv->irq = config->ibirq;
	ines_online(ines_priv, board, 1);
	return 0;
}

void ines_pci_detach(gpib_board_t *board)
{
	struct ines_priv *ines_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (ines_priv) {
		nec_priv = &ines_priv->nec7210_priv;
		if (ines_priv->irq) {
			// disable interrupts
			switch (ines_priv->pci_chip_type) {
			case PCI_CHIP_AMCC5920:
				if (ines_priv->plx_iobase)
					outl(0, ines_priv->plx_iobase + PLX9050_INTCSR_REG);
				break;
			case PCI_CHIP_QUANCOM:
				if (nec_priv->iobase)
					outb(0, nec_priv->iobase +
					     QUANCOM_IRQ_CONTROL_STATUS_REG);
				break;
			default:
				break;
			}
			free_irq(ines_priv->irq, board);
		}
		if (nec_priv->iobase) {
			nec7210_board_reset(nec_priv, board);
			pci_release_regions(ines_priv->pci_device);
		}
		if (ines_priv->pci_device)
			pci_dev_put(ines_priv->pci_device);
	}
	ines_free_private(board);
}

void ines_isa_detach(gpib_board_t *board)
{
	struct ines_priv *ines_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (ines_priv) {
		nec_priv = &ines_priv->nec7210_priv;
		if (ines_priv->irq)
			free_irq(ines_priv->irq, board);
		if (nec_priv->iobase) {
			nec7210_board_reset(nec_priv, board);
			release_region(nec_priv->iobase, ines_isa_iosize);
		}
	}
	ines_free_private(board);
}

static int ines_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	return 0;
}

static struct pci_driver ines_pci_driver = {
	.name = "ines_gpib",
	.id_table = ines_pci_table,
	.probe = &ines_pci_probe
};

#ifdef GPIB_PCMCIA

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/timer.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
#define DEBUG(n, args...) do {if (pc_debug > (n)) pr_debug(args)} while (0)
#else
#define DEBUG(args...)
#endif

static const int ines_pcmcia_iosize = 0x20;

/*    The event() function is this driver's Card Services event handler.
 *    It will be called by Card Services when an appropriate card status
 *    event is received.  The config() and release() entry points are
 *    used to configure or release a socket, in response to card insertion
 *    and ejection events.  They are invoked from the gpib event
 *    handler.
 */

static int ines_gpib_config(struct pcmcia_device  *link);
static void ines_gpib_release(struct pcmcia_device  *link);
static int ines_pcmcia_attach(gpib_board_t *board, const gpib_board_config_t *config);
static int ines_pcmcia_accel_attach(gpib_board_t *board, const gpib_board_config_t *config);
static void ines_pcmcia_detach(gpib_board_t *board);
static irqreturn_t ines_pcmcia_interrupt(int irq, void *arg);
static int ines_common_pcmcia_attach(gpib_board_t *board);
/*
 * A linked list of "instances" of the gpib device.  Each actual
 *  PCMCIA card corresponds to one device instance, and is described
 *  by one dev_link_t structure (defined in ds.h).
 *
 *  You may not want to use a linked list for this -- for example, the
 *  memory card driver uses an array of dev_link_t pointers, where minor
 *  device numbers are used to derive the corresponding array index.
 */

static struct pcmcia_device *curr_dev;

/*
 *   A dev_link_t structure has fields for most things that are needed
 *  to keep track of a socket, but there will usually be some device
 *  specific information that also needs to be kept track of.  The
 *  'priv' pointer in a dev_link_t structure can be used to point to
 *  a device-specific private data structure, like this.
 *
 *  A driver needs to provide a dev_node_t structure for each device
 *  on a card.	In some cases, there is only one device per card (for
 *  example, ethernet cards, modems).  In other cases, there may be
 *  many actual or logical devices (SCSI adapters, memory cards with
 *  multiple partitions).  The dev_node_t structures need to be kept
 *  in a linked list starting at the 'dev' field of a dev_link_t
 *  structure.	We allocate them in the card's private data structure,
 *  because they generally can't be allocated dynamically.
 */

struct local_info {
	struct pcmcia_device	*p_dev;
	gpib_board_t		*dev;
	u_short manfid;
	u_short cardid;
};

/*
 *   gpib_attach() creates an "instance" of the driver, allocating
 *   local data structures for one device.  The device is registered
 *   with Card Services.
 *
 *   The dev_link structure is initialized, but we don't actually
 *   configure the card at this point -- we wait until we receive a
 *   card insertion event.
 */
static int ines_gpib_probe(struct pcmcia_device *link)
{
	struct local_info *info;

//	int ret, i;

	DEBUG(0, "%s(0x%p)\n", __func__ link);

	/* Allocate space for private device-specific data */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->p_dev = link;
	link->priv = info;

	/* The io structure describes IO port mapping */
	link->resource[0]->end = 32;
	link->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	link->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	link->io_lines = 5;

	/* General socket configuration */
	link->config_flags = CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	/* Register with Card Services */
	curr_dev = link;
	return ines_gpib_config(link);
}

/*
 *   This deletes a driver "instance".	The device is de-registered
 *   with Card Services.  If it has been released, all local data
 *   structures are freed.  Otherwise, the structures will be freed
 *   when the device is released.
 */
static void ines_gpib_remove(struct pcmcia_device *link)
{
	struct local_info *info = link->priv;
	//struct gpib_board_t *dev = info->dev;

	DEBUG(0, "%s(0x%p)\n", __func__, link);

	if (info->dev)
		ines_pcmcia_detach(info->dev);
	ines_gpib_release(link);

	//free_netdev(dev);
	kfree(info);
}

static int ines_gpib_config_iteration(struct pcmcia_device *link, void *priv_data)
{
	return pcmcia_request_io(link);
}

/*
 *   gpib_config() is scheduled to run after a CARD_INSERTION event
 *   is received, to configure the PCMCIA socket, and to make the
 *   device available to the system.
 */
static int ines_gpib_config(struct pcmcia_device *link)
{
	struct local_info *dev;
	int retval;
	void __iomem *virt;

	dev = link->priv;
	DEBUG(0, "%s(0x%p)\n", __func__, link);

	retval = pcmcia_loop_config(link, &ines_gpib_config_iteration, NULL);
	if (retval) {
		dev_warn(&link->dev, "no configuration found\n");
		ines_gpib_release(link);
		return -ENODEV;
	}

	pr_debug("ines_cs: manufacturer: 0x%x card: 0x%x\n",
		 link->manf_id, link->card_id);

	/*  for the ines card we have to setup the configuration registers in
	 *	attribute memory here
	 */
	link->resource[2]->flags |= WIN_MEMORY_TYPE_AM | WIN_DATA_WIDTH_8 | WIN_ENABLE;
	link->resource[2]->end = 0x1000;
	retval = pcmcia_request_window(link, link->resource[2], 250);
	if (retval) {
		dev_warn(&link->dev, "pcmcia_request_window failed\n");
		ines_gpib_release(link);
		return -ENODEV;
	}
	retval = pcmcia_map_mem_page(link, link->resource[2], 0);
	if (retval) {
		dev_warn(&link->dev, "pcmcia_map_mem_page failed\n");
		ines_gpib_release(link);
		return -ENODEV;
	}
	virt = ioremap(link->resource[2]->start, resource_size(link->resource[2]));
	writeb((link->resource[2]->start >> 2) & 0xff, virt + 0xf0); // IOWindow base
	iounmap(virt);

	/*
	 * This actually configures the PCMCIA socket -- setting up
	 * the I/O windows and the interrupt mapping.
	 */
	retval = pcmcia_enable_device(link);
	if (retval) {
		ines_gpib_release(link);
		return -ENODEV;
	}
	pr_info("ines gpib device loaded\n");
	return 0;
} /* gpib_config */

/*
 *   After a card is removed, gpib_release() will unregister the net
 *   device, and release the PCMCIA configuration.  If the device is
 *   still open, this will be postponed until it is closed.
 */

static void ines_gpib_release(struct pcmcia_device *link)
{
	DEBUG(0, "%s(0x%p)\n", __func__, link);
	pcmcia_disable_device(link);
} /* gpib_release */

static int ines_gpib_suspend(struct pcmcia_device *link)
{
	//struct local_info *info = link->priv;
	//struct gpib_board_t *dev = info->dev;
	DEBUG(0, "%s(0x%p)\n", __func__, link);

	if (link->open)
		pr_err("Device still open ???\n");
	//netif_device_detach(dev);

	return 0;
}

static int ines_gpib_resume(struct pcmcia_device *link)
{
	//struct local_info_t *info = link->priv;
	//struct gpib_board_t *dev = info->dev;
	DEBUG(0, "%s(0x%p)\n", __func__, link);

	/*if (link->open) {
	 *	ni_gpib_probe(dev);	/ really?
	 *		printk("Gpib resumed ???\n");
	 *	//netif_device_attach(dev);
	 *}
	 */
	return ines_gpib_config(link);
}

static struct pcmcia_device_id ines_pcmcia_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01b4, 0x4730),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, ines_pcmcia_ids);

static struct pcmcia_driver ines_gpib_cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "ines_gpib_cs",
	.id_table	= ines_pcmcia_ids,
	.probe		= ines_gpib_probe,
	.remove		= ines_gpib_remove,
	.suspend	= ines_gpib_suspend,
	.resume		= ines_gpib_resume,
};

void ines_pcmcia_cleanup_module(void)
{
	DEBUG(0, "ines_cs: unloading\n");
	pcmcia_unregister_driver(&ines_gpib_cs_driver);
}

static gpib_interface_t ines_pcmcia_unaccel_interface = {
	.name = "ines_pcmcia_unaccel",
	.attach = ines_pcmcia_attach,
	.detach = ines_pcmcia_detach,
	.read = ines_read,
	.write = ines_write,
	.command = ines_command,
	.take_control = ines_take_control,
	.go_to_standby = ines_go_to_standby,
	.request_system_control = ines_request_system_control,
	.interface_clear = ines_interface_clear,
	.remote_enable = ines_remote_enable,
	.enable_eos = ines_enable_eos,
	.disable_eos = ines_disable_eos,
	.parallel_poll = ines_parallel_poll,
	.parallel_poll_configure = ines_parallel_poll_configure,
	.parallel_poll_response = ines_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ines_line_status,
	.update_status = ines_update_status,
	.primary_address = ines_primary_address,
	.secondary_address = ines_secondary_address,
	.serial_poll_response = ines_serial_poll_response,
	.serial_poll_status = ines_serial_poll_status,
	.t1_delay = ines_t1_delay,
	.return_to_local = ines_return_to_local,
};

static gpib_interface_t ines_pcmcia_accel_interface = {
	.name = "ines_pcmcia_accel",
	.attach = ines_pcmcia_accel_attach,
	.detach = ines_pcmcia_detach,
	.read = ines_accel_read,
	.write = ines_accel_write,
	.command = ines_command,
	.take_control = ines_take_control,
	.go_to_standby = ines_go_to_standby,
	.request_system_control = ines_request_system_control,
	.interface_clear = ines_interface_clear,
	.remote_enable = ines_remote_enable,
	.enable_eos = ines_enable_eos,
	.disable_eos = ines_disable_eos,
	.parallel_poll = ines_parallel_poll,
	.parallel_poll_configure = ines_parallel_poll_configure,
	.parallel_poll_response = ines_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ines_line_status,
	.update_status = ines_update_status,
	.primary_address = ines_primary_address,
	.secondary_address = ines_secondary_address,
	.serial_poll_response = ines_serial_poll_response,
	.serial_poll_status = ines_serial_poll_status,
	.t1_delay = ines_t1_delay,
	.return_to_local = ines_return_to_local,
};

static gpib_interface_t ines_pcmcia_interface = {
	.name = "ines_pcmcia",
	.attach = ines_pcmcia_accel_attach,
	.detach = ines_pcmcia_detach,
	.read = ines_accel_read,
	.write = ines_accel_write,
	.command = ines_command,
	.take_control = ines_take_control,
	.go_to_standby = ines_go_to_standby,
	.request_system_control = ines_request_system_control,
	.interface_clear = ines_interface_clear,
	.remote_enable = ines_remote_enable,
	.enable_eos = ines_enable_eos,
	.disable_eos = ines_disable_eos,
	.parallel_poll = ines_parallel_poll,
	.parallel_poll_configure = ines_parallel_poll_configure,
	.parallel_poll_response = ines_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ines_line_status,
	.update_status = ines_update_status,
	.primary_address = ines_primary_address,
	.secondary_address = ines_secondary_address,
	.serial_poll_response = ines_serial_poll_response,
	.serial_poll_status = ines_serial_poll_status,
	.t1_delay = ines_t1_delay,
	.return_to_local = ines_return_to_local,
};

irqreturn_t ines_pcmcia_interrupt(int irq, void *arg)
{
	gpib_board_t *board = arg;

	return ines_interrupt(board);
}

int ines_common_pcmcia_attach(gpib_board_t *board)
{
	struct ines_priv *ines_priv;
	struct nec7210_priv *nec_priv;
	int retval;

	if (!curr_dev) {
		pr_err("no ines pcmcia cards found\n");
		return -1;
	}

	retval = ines_generic_attach(board);
	if (retval)
		return retval;

	ines_priv = board->private_data;
	nec_priv = &ines_priv->nec7210_priv;

	if (!request_region(curr_dev->resource[0]->start,
			    resource_size(curr_dev->resource[0]), "ines_gpib")) {
		pr_err("ines_gpib: ioports at 0x%lx already in use\n",
		       (unsigned long)(curr_dev->resource[0]->start));
		return -1;
	}

	nec_priv->iobase = curr_dev->resource[0]->start;

	nec7210_board_reset(nec_priv, board);

	if (request_irq(curr_dev->irq, ines_pcmcia_interrupt, IRQF_SHARED,
			"pcmcia-gpib", board))	{
		pr_err("gpib: can't request IRQ %d\n", curr_dev->irq);
		return -1;
	}
	ines_priv->irq = curr_dev->irq;

	return 0;
}

int ines_pcmcia_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct ines_priv *ines_priv;
	int retval;

	retval = ines_common_pcmcia_attach(board);
	if (retval < 0)
		return retval;

	ines_priv = board->private_data;
	ines_online(ines_priv, board, 0);

	return 0;
}

int ines_pcmcia_accel_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct ines_priv *ines_priv;
	int retval;

	retval = ines_common_pcmcia_attach(board);
	if (retval < 0)
		return retval;

	ines_priv = board->private_data;
	ines_online(ines_priv, board, 1);

	return 0;
}

void ines_pcmcia_detach(gpib_board_t *board)
{
	struct ines_priv *ines_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (ines_priv) {
		nec_priv = &ines_priv->nec7210_priv;
		if (ines_priv->irq)
			free_irq(ines_priv->irq, board);
		if (nec_priv->iobase) {
			nec7210_board_reset(nec_priv, board);
			release_region(nec_priv->iobase, ines_pcmcia_iosize);
		}
	}
	ines_free_private(board);
}

#endif /* GPIB_PCMCIA */

static int __init ines_init_module(void)
{
	int ret;

	ret = pci_register_driver(&ines_pci_driver);
	if (ret) {
		pr_err("ines_gpib: pci_register_driver failed: error = %d\n", ret);
		return ret;
	}

	ret = gpib_register_driver(&ines_pci_interface, THIS_MODULE);
	if (ret) {
		pr_err("ines_gpib: gpib_register_driver failed: error = %d\n", ret);
		goto err_pci;
	}

	ret = gpib_register_driver(&ines_pci_unaccel_interface, THIS_MODULE);
	if (ret) {
		pr_err("ines_gpib: gpib_register_driver failed: error = %d\n", ret);
		goto err_pci_unaccel;
	}

	ret = gpib_register_driver(&ines_pci_accel_interface, THIS_MODULE);
	if (ret) {
		pr_err("ines_gpib: gpib_register_driver failed: error = %d\n", ret);
		goto err_pci_accel;
	}

	ret = gpib_register_driver(&ines_isa_interface, THIS_MODULE);
	if (ret) {
		pr_err("ines_gpib: gpib_register_driver failed: error = %d\n", ret);
		goto err_isa;
	}

#ifdef GPIB_PCMCIA
	ret = gpib_register_driver(&ines_pcmcia_interface, THIS_MODULE);
	if (ret) {
		pr_err("ines_gpib: gpib_register_driver failed: error = %d\n", ret);
		goto err_pcmcia;
	}

	ret = gpib_register_driver(&ines_pcmcia_unaccel_interface, THIS_MODULE);
	if (ret) {
		pr_err("ines_gpib: gpib_register_driver failed: error = %d\n", ret);
		goto err_pcmcia_unaccel;
	}

	ret = gpib_register_driver(&ines_pcmcia_accel_interface, THIS_MODULE);
	if (ret) {
		pr_err("ines_gpib: gpib_register_driver failed: error = %d\n", ret);
		goto err_pcmcia_accel;
	}

	ret = pcmcia_register_driver(&ines_gpib_cs_driver);
	if (ret) {
		pr_err("ines_gpib: pcmcia_register_driver failed: error = %d\n", ret);
		goto err_pcmcia_driver;
	}
#endif

	return 0;

#ifdef GPIB_PCMCIA
err_pcmcia_driver:
	gpib_unregister_driver(&ines_pcmcia_accel_interface);
err_pcmcia_accel:
	gpib_unregister_driver(&ines_pcmcia_unaccel_interface);
err_pcmcia_unaccel:
	gpib_unregister_driver(&ines_pcmcia_interface);
err_pcmcia:
#endif
	gpib_unregister_driver(&ines_isa_interface);
err_isa:
	gpib_unregister_driver(&ines_pci_accel_interface);
err_pci_accel:
	gpib_unregister_driver(&ines_pci_unaccel_interface);
err_pci_unaccel:
	gpib_unregister_driver(&ines_pci_interface);
err_pci:
	pci_unregister_driver(&ines_pci_driver);

	return ret;
}

static void __exit ines_exit_module(void)
{
	gpib_unregister_driver(&ines_pci_interface);
	gpib_unregister_driver(&ines_pci_unaccel_interface);
	gpib_unregister_driver(&ines_pci_accel_interface);
	gpib_unregister_driver(&ines_isa_interface);
#ifdef GPIB__PCMCIA
	gpib_unregister_driver(&ines_pcmcia_interface);
	gpib_unregister_driver(&ines_pcmcia_unaccel_interface);
	gpib_unregister_driver(&ines_pcmcia_accel_interface);
	ines_pcmcia_cleanup_module();
#endif

	pci_unregister_driver(&ines_pci_driver);
}

module_init(ines_init_module);
module_exit(ines_exit_module);
