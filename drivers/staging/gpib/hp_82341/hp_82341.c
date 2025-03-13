// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *     Driver for hp 82341a/b/c/d boards.                                  *
 * Might be worth merging with Agilent 82350b driver.                      *
 *   copyright            : (C) 2002, 2005 by Frank Mori Hess              *
 ***************************************************************************/

#include "hp_82341.h"
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/isapnp.h>

MODULE_LICENSE("GPL");

int hp_82341_accel_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end,
			size_t *bytes_read)
{
	struct hp_82341_priv *hp_priv = board->private_data;
	struct tms9914_priv *tms_priv = &hp_priv->tms9914_priv;
	int retval = 0;
	unsigned short event_status;
	int i;
	int num_fifo_bytes;
	//hardware doesn't support checking for end-of-string character when using fifo
	if (tms_priv->eos_flags & REOS)
		return tms9914_read(board, tms_priv, buffer, length, end, bytes_read);

	clear_bit(DEV_CLEAR_BN, &tms_priv->state);

	read_and_clear_event_status(board);
	*end = 0;
	*bytes_read = 0;
	if (length == 0)
		return 0;
	//disable fifo for the moment
	outb(DIRECTION_GPIB_TO_HOST_BIT, hp_priv->iobase[3] + BUFFER_CONTROL_REG);
	// Handle corner case of board not in holdoff and one byte has slipped in already.
	// Also, board sometimes has problems (spurious 1 byte reads) when read fifo is
	// started up with board in
	// TACS under certain data holdoff conditions.  Doing a 1 byte tms9914-style
	// read avoids these problems.
	if (/*tms_priv->holdoff_active == 0 && */length > 1) {
		size_t num_bytes;

		retval = tms9914_read(board, tms_priv, buffer, 1, end, &num_bytes);
		*bytes_read += num_bytes;
		if (retval < 0)
			pr_err("tms9914_read failed retval=%i\n", retval);
		if (retval < 0 || *end)
			return retval;
		++buffer;
		--length;
	}
	tms9914_set_holdoff_mode(tms_priv, TMS9914_HOLDOFF_EOI);
	tms9914_release_holdoff(tms_priv);
	outb(0x00, hp_priv->iobase[3] + BUFFER_FLUSH_REG);
	i = 0;
	num_fifo_bytes = length - 1;
	while (i < num_fifo_bytes && *end == 0)	{
		int block_size;
		int j;
		int count;

		if (num_fifo_bytes - i < hp_82341_fifo_size)
			block_size = num_fifo_bytes - i;
		else
			block_size = hp_82341_fifo_size;
		set_transfer_counter(hp_priv, block_size);
		outb(ENABLE_TI_BUFFER_BIT | DIRECTION_GPIB_TO_HOST_BIT, hp_priv->iobase[3] +
		     BUFFER_CONTROL_REG);
		if (inb(hp_priv->iobase[0] + STREAM_STATUS_REG) & HALTED_STATUS_BIT)
			outb(RESTART_STREAM_BIT, hp_priv->iobase[0] + STREAM_STATUS_REG);

		clear_bit(READ_READY_BN, &tms_priv->state);

		retval = wait_event_interruptible(board->wait,
						  ((event_status =
						    read_and_clear_event_status(board)) &
						   (TERMINAL_COUNT_EVENT_BIT |
						    BUFFER_END_EVENT_BIT)) ||
						  test_bit(DEV_CLEAR_BN, &tms_priv->state) ||
						  test_bit(TIMO_NUM, &board->status));
		if (retval)  {
			pr_warn("%s: read wait interrupted\n", __func__);
			retval = -ERESTARTSYS;
			break;
		}
		// have to disable buffer before we can read from buffer port
		outb(DIRECTION_GPIB_TO_HOST_BIT, hp_priv->iobase[3] + BUFFER_CONTROL_REG);
		count = block_size - read_transfer_counter(hp_priv);
		j = 0;
		while (j < count && i < num_fifo_bytes) {
			unsigned short data_word = inw(hp_priv->iobase[3] + BUFFER_PORT_LOW_REG);

			buffer[i++] = data_word & 0xff;
			++j;
			if (j < count && i < num_fifo_bytes) {
				buffer[i++] = (data_word >> 8) & 0xff;
				++j;
			}
		}
		if (event_status & BUFFER_END_EVENT_BIT) {
			clear_bit(RECEIVED_END_BN, &tms_priv->state);

			*end = 1;
			tms_priv->holdoff_active = 1;
		}
		if (test_bit(TIMO_NUM, &board->status))	{
			pr_debug("%s: minor %i: read timed out\n", __FILE__, board->minor);
			retval = -ETIMEDOUT;
			break;
		}
		if (test_bit(DEV_CLEAR_BN, &tms_priv->state)) {
			pr_warn("%s: device clear interrupted read\n", __FILE__);
			retval = -EINTR;
			break;
		}
	}
	*bytes_read += i;
	buffer += i;
	length -= i;
	if (retval < 0)
		return retval;
	// read last byte if we havn't received an END yet
	if (*end == 0) {
		size_t num_bytes;
		// try to make sure we holdoff after last byte read
		retval = tms9914_read(board, tms_priv, buffer, length, end, &num_bytes);
		*bytes_read += num_bytes;
		if (retval < 0)
			return retval;
	}
	return 0;
}

static int restart_write_fifo(gpib_board_t *board, struct hp_82341_priv *hp_priv)
{
	struct tms9914_priv *tms_priv = &hp_priv->tms9914_priv;

	if ((inb(hp_priv->iobase[0] + STREAM_STATUS_REG) & HALTED_STATUS_BIT) == 0)
		return 0;
	while (1) {
		int status;

		//restart doesn't work if data holdoff is in effect
		status = tms9914_line_status(board, tms_priv);
		if ((status & BusNRFD) == 0) {
			outb(RESTART_STREAM_BIT, hp_priv->iobase[0] + STREAM_STATUS_REG);
			return 0;
		}
		if (test_bit(DEV_CLEAR_BN, &tms_priv->state))
			return -EINTR;
		if (test_bit(TIMO_NUM, &board->status))
			return -ETIMEDOUT;
		if (msleep_interruptible(1))
			return -EINTR;
	}
	return 0;
}

int hp_82341_accel_write(gpib_board_t *board, uint8_t *buffer, size_t length,
			 int send_eoi, size_t *bytes_written)
{
	struct hp_82341_priv *hp_priv = board->private_data;
	struct tms9914_priv *tms_priv = &hp_priv->tms9914_priv;
	int i, j;
	unsigned short event_status;
	int retval = 0;
	int fifo_xfer_len = length;

	*bytes_written = 0;
	if (send_eoi)
		--fifo_xfer_len;

	clear_bit(DEV_CLEAR_BN, &tms_priv->state);

	read_and_clear_event_status(board);
	outb(0, hp_priv->iobase[3] + BUFFER_CONTROL_REG);
	outb(0x00, hp_priv->iobase[3] + BUFFER_FLUSH_REG);
	for (i = 0; i < fifo_xfer_len;) {
		int block_size;

		if (fifo_xfer_len - i < hp_82341_fifo_size)
			block_size = fifo_xfer_len - i;
		else
			block_size = hp_82341_fifo_size;
		set_transfer_counter(hp_priv, block_size);
		// load data into board's fifo
		for (j = 0; j < block_size;) {
			unsigned short data_word = buffer[i++];
			++j;
			if (j < block_size) {
				data_word |= buffer[i++] << 8;
				++j;
			}
			outw(data_word, hp_priv->iobase[3] + BUFFER_PORT_LOW_REG);
		}
		clear_bit(WRITE_READY_BN, &tms_priv->state);
		outb(ENABLE_TI_BUFFER_BIT, hp_priv->iobase[3] + BUFFER_CONTROL_REG);
		retval = restart_write_fifo(board, hp_priv);
		if (retval < 0)	{
			pr_err("hp82341: failed to restart write stream\n");
			break;
		}
		retval = wait_event_interruptible(board->wait,
						  ((event_status =
						    read_and_clear_event_status(board)) &
						   TERMINAL_COUNT_EVENT_BIT) ||
						  test_bit(DEV_CLEAR_BN, &tms_priv->state) ||
						  test_bit(TIMO_NUM, &board->status));
		outb(0, hp_priv->iobase[3] + BUFFER_CONTROL_REG);
		*bytes_written += block_size - read_transfer_counter(hp_priv);
		if (retval) {
			pr_warn("%s: write wait interrupted\n", __FILE__);
			retval = -ERESTARTSYS;
			break;
		}
		if (test_bit(TIMO_NUM, &board->status))	{
			pr_debug("%s: minor %i: write timed out\n", __FILE__, board->minor);
			retval = -ETIMEDOUT;
			break;
		}
		if (test_bit(DEV_CLEAR_BN, &tms_priv->state)) {
			pr_warn("%s: device clear interrupted write\n", __FILE__);
			retval = -EINTR;
			break;
		}
	}
	if (retval)
		return retval;
	if (send_eoi) {
		size_t num_bytes;

		retval = hp_82341_write(board, buffer + fifo_xfer_len, 1, 1, &num_bytes);
		*bytes_written += num_bytes;
		if (retval < 0)
			return retval;
	}
	return 0;
}

static int hp_82341_attach(gpib_board_t *board, const gpib_board_config_t *config);

static void hp_82341_detach(gpib_board_t *board);

// wrappers for interface functions
int hp_82341_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end, size_t *bytes_read)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_read(board, &priv->tms9914_priv, buffer, length, end, bytes_read);
}

int hp_82341_write(gpib_board_t *board, uint8_t *buffer, size_t length, int send_eoi,
		   size_t *bytes_written)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_write(board, &priv->tms9914_priv, buffer, length, send_eoi, bytes_written);
}

int hp_82341_command(gpib_board_t *board, uint8_t *buffer, size_t length, size_t *bytes_written)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_command(board, &priv->tms9914_priv, buffer, length, bytes_written);
}

int hp_82341_take_control(gpib_board_t *board, int synchronous)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_take_control(board, &priv->tms9914_priv, synchronous);
}

int hp_82341_go_to_standby(gpib_board_t *board)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_go_to_standby(board, &priv->tms9914_priv);
}

void hp_82341_request_system_control(gpib_board_t *board, int request_control)
{
	struct hp_82341_priv *priv = board->private_data;

	if (request_control)
		priv->mode_control_bits |= SYSTEM_CONTROLLER_BIT;
	else
		priv->mode_control_bits &= ~SYSTEM_CONTROLLER_BIT;
	outb(priv->mode_control_bits, priv->iobase[0] + MODE_CONTROL_STATUS_REG);
	tms9914_request_system_control(board, &priv->tms9914_priv, request_control);
}

void hp_82341_interface_clear(gpib_board_t *board, int assert)
{
	struct hp_82341_priv *priv = board->private_data;

	tms9914_interface_clear(board, &priv->tms9914_priv, assert);
}

void hp_82341_remote_enable(gpib_board_t *board, int enable)
{
	struct hp_82341_priv *priv = board->private_data;

	tms9914_remote_enable(board, &priv->tms9914_priv, enable);
}

int hp_82341_enable_eos(gpib_board_t *board, uint8_t eos_byte, int compare_8_bits)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_enable_eos(board, &priv->tms9914_priv, eos_byte, compare_8_bits);
}

void hp_82341_disable_eos(gpib_board_t *board)
{
	struct hp_82341_priv *priv = board->private_data;

	tms9914_disable_eos(board, &priv->tms9914_priv);
}

unsigned int hp_82341_update_status(gpib_board_t *board, unsigned int clear_mask)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_update_status(board, &priv->tms9914_priv, clear_mask);
}

int hp_82341_primary_address(gpib_board_t *board, unsigned int address)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_primary_address(board, &priv->tms9914_priv, address);
}

int hp_82341_secondary_address(gpib_board_t *board, unsigned int address, int enable)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_secondary_address(board, &priv->tms9914_priv, address, enable);
}

int hp_82341_parallel_poll(gpib_board_t *board, uint8_t *result)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_parallel_poll(board, &priv->tms9914_priv, result);
}

void hp_82341_parallel_poll_configure(gpib_board_t *board, uint8_t config)
{
	struct hp_82341_priv *priv = board->private_data;

	tms9914_parallel_poll_configure(board, &priv->tms9914_priv, config);
}

void hp_82341_parallel_poll_response(gpib_board_t *board, int ist)
{
	struct hp_82341_priv *priv = board->private_data;

	tms9914_parallel_poll_response(board, &priv->tms9914_priv, ist);
}

void hp_82341_serial_poll_response(gpib_board_t *board, uint8_t status)
{
	struct hp_82341_priv *priv = board->private_data;

	tms9914_serial_poll_response(board, &priv->tms9914_priv, status);
}

static uint8_t hp_82341_serial_poll_status(gpib_board_t *board)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_serial_poll_status(board, &priv->tms9914_priv);
}

static int hp_82341_line_status(const gpib_board_t *board)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_line_status(board, &priv->tms9914_priv);
}

static unsigned int hp_82341_t1_delay(gpib_board_t *board, unsigned int nano_sec)
{
	struct hp_82341_priv *priv = board->private_data;

	return tms9914_t1_delay(board, &priv->tms9914_priv, nano_sec);
}

void hp_82341_return_to_local(gpib_board_t *board)
{
	struct hp_82341_priv *priv = board->private_data;

	tms9914_return_to_local(board, &priv->tms9914_priv);
}

static gpib_interface_t hp_82341_unaccel_interface = {
	.name = "hp_82341_unaccel",
	.attach = hp_82341_attach,
	.detach = hp_82341_detach,
	.read = hp_82341_read,
	.write = hp_82341_write,
	.command = hp_82341_command,
	.request_system_control = hp_82341_request_system_control,
	.take_control = hp_82341_take_control,
	.go_to_standby = hp_82341_go_to_standby,
	.interface_clear = hp_82341_interface_clear,
	.remote_enable = hp_82341_remote_enable,
	.enable_eos = hp_82341_enable_eos,
	.disable_eos = hp_82341_disable_eos,
	.parallel_poll = hp_82341_parallel_poll,
	.parallel_poll_configure = hp_82341_parallel_poll_configure,
	.parallel_poll_response = hp_82341_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = hp_82341_line_status,
	.update_status = hp_82341_update_status,
	.primary_address = hp_82341_primary_address,
	.secondary_address = hp_82341_secondary_address,
	.serial_poll_response = hp_82341_serial_poll_response,
	.serial_poll_status = hp_82341_serial_poll_status,
	.t1_delay = hp_82341_t1_delay,
	.return_to_local = hp_82341_return_to_local,
};

static gpib_interface_t hp_82341_interface = {
	.name = "hp_82341",
	.attach = hp_82341_attach,
	.detach = hp_82341_detach,
	.read = hp_82341_accel_read,
	.write = hp_82341_accel_write,
	.command = hp_82341_command,
	.request_system_control = hp_82341_request_system_control,
	.take_control = hp_82341_take_control,
	.go_to_standby = hp_82341_go_to_standby,
	.interface_clear = hp_82341_interface_clear,
	.remote_enable = hp_82341_remote_enable,
	.enable_eos = hp_82341_enable_eos,
	.disable_eos = hp_82341_disable_eos,
	.parallel_poll = hp_82341_parallel_poll,
	.parallel_poll_configure = hp_82341_parallel_poll_configure,
	.parallel_poll_response = hp_82341_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = hp_82341_line_status,
	.update_status = hp_82341_update_status,
	.primary_address = hp_82341_primary_address,
	.secondary_address = hp_82341_secondary_address,
	.serial_poll_response = hp_82341_serial_poll_response,
	.t1_delay = hp_82341_t1_delay,
	.return_to_local = hp_82341_return_to_local,
};

int hp_82341_allocate_private(gpib_board_t *board)
{
	board->private_data = kzalloc(sizeof(struct hp_82341_priv), GFP_KERNEL);
	if (!board->private_data)
		return -ENOMEM;
	return 0;
}

void hp_82341_free_private(gpib_board_t *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

static uint8_t hp_82341_read_byte(struct tms9914_priv *priv, unsigned int register_num)
{
	return inb(priv->iobase + register_num);
}

static void hp_82341_write_byte(struct tms9914_priv *priv, uint8_t data, unsigned int register_num)
{
	outb(data, priv->iobase + register_num);
}

static int hp_82341_find_isapnp_board(struct pnp_dev **dev)
{
	*dev = pnp_find_dev(NULL, ISAPNP_VENDOR('H', 'W', 'P'),
			    ISAPNP_FUNCTION(0x1411), NULL);
	if (!*dev || !(*dev)->card) {
		pr_err("hp_82341: failed to find isapnp board\n");
		return -ENODEV;
	}
	if (pnp_device_attach(*dev) < 0) {
		pr_err("hp_82341: board already active, skipping\n");
		return -EBUSY;
	}
	if (pnp_activate_dev(*dev) < 0) {
		pnp_device_detach(*dev);
		pr_err("hp_82341: failed to activate() atgpib/tnt, aborting\n");
		return -EAGAIN;
	}
	if (!pnp_port_valid(*dev, 0) || !pnp_irq_valid(*dev, 0)) {
		pnp_device_detach(*dev);
		pr_err("hp_82341: invalid port or irq for atgpib/tnt, aborting\n");
		return -ENOMEM;
	}
	return 0;
}

static int xilinx_ready(struct hp_82341_priv *hp_priv)
{
	switch (hp_priv->hw_version) {
	case HW_VERSION_82341C:
		if (inb(hp_priv->iobase[0] + CONFIG_CONTROL_STATUS_REG) & XILINX_READY_BIT)
			return 1;
		else
			return 0;
		break;
	case HW_VERSION_82341D:
		if (isapnp_read_byte(PIO_DATA_REG) & HP_82341D_XILINX_READY_BIT)
			return 1;
		else
			return 0;
	default:
		pr_err("hp_82341: %s: bug! unknown hw_version\n", __func__);
		break;
	}
	return 0;
}

static int xilinx_done(struct hp_82341_priv *hp_priv)
{
	switch (hp_priv->hw_version) {
	case HW_VERSION_82341C:
		if (inb(hp_priv->iobase[0] + CONFIG_CONTROL_STATUS_REG) & DONE_PGL_BIT)
			return 1;
		else
			return 0;
	case HW_VERSION_82341D:
		if (isapnp_read_byte(PIO_DATA_REG) & HP_82341D_XILINX_DONE_BIT)
			return 1;
		else
			return 0;
	default:
		pr_err("hp_82341: %s: bug! unknown hw_version\n", __func__);
		break;
	}
	return 0;
}

static int irq_valid(struct hp_82341_priv *hp_priv, int irq)
{
	switch (hp_priv->hw_version) {
	case HW_VERSION_82341C:
		switch (irq) {
		case 3:
		case 5:
		case 7:
		case 9:
		case 10:
		case 11:
		case 12:
		case 15:
			return 1;
		default:
			pr_err("hp_82341: invalid irq=%i for 82341C, irq must be 3, 5, 7, 9, 10, 11, 12, or 15.\n",
			       irq);
			return 0;
		}
		break;
	case HW_VERSION_82341D:
		return 1;
	default:
		pr_err("hp_82341: %s: bug! unknown hw_version\n", __func__);
		break;
	}
	return 0;
}

static int hp_82341_load_firmware_array(struct hp_82341_priv *hp_priv,
					const unsigned char *firmware_data,
					unsigned int firmware_length)
{
	int i, j;
	static const int timeout = 100;

	for (i = 0; i < firmware_length; ++i) {
		for (j = 0; j < timeout; ++j) {
			if (need_resched())
				schedule();
			if (xilinx_ready(hp_priv))
				break;
			usleep_range(10, 15);
		}
		if (j == timeout) {
			pr_err("hp_82341: timed out waiting for Xilinx ready.\n");
			return -ETIMEDOUT;
		}
		outb(firmware_data[i], hp_priv->iobase[0] + XILINX_DATA_REG);
	}
	for (j = 0; j < timeout; ++j) {
		if (xilinx_done(hp_priv))
			break;
		if (need_resched())
			schedule();
		usleep_range(10, 15);
	}
	if (j == timeout) {
		pr_err("hp_82341: timed out waiting for Xilinx done.\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static int hp_82341_load_firmware(struct hp_82341_priv *hp_priv, const gpib_board_config_t *config)
{
	if (config->init_data_length == 0) {
		if (xilinx_done(hp_priv))
			return 0;
		pr_err("hp_82341: board needs be initialized with firmware upload.\n"
		       "\tUse the --init-data option of gpib_config.\n");
		return -EINVAL;
	}
	switch (hp_priv->hw_version) {
	case HW_VERSION_82341C:
		if (config->init_data_length != hp_82341c_firmware_length) {
			pr_err("hp_82341: bad firmware length=%i for 82341c (expected %i).\n",
			       config->init_data_length, hp_82341c_firmware_length);
			return -EINVAL;
		}
		break;
	case HW_VERSION_82341D:
		if (config->init_data_length != hp_82341d_firmware_length) {
			pr_err("hp_82341: bad firmware length=%i for 82341d (expected %i).\n",
			       config->init_data_length, hp_82341d_firmware_length);
			return -EINVAL;
		}
		break;
	default:
		pr_err("hp_82341: %s: bug! unknown hw_version\n", __func__);
		break;
	}
	return hp_82341_load_firmware_array(hp_priv, config->init_data, config->init_data_length);
}

static void set_xilinx_not_prog(struct hp_82341_priv *hp_priv, int assert)
{
	switch (hp_priv->hw_version) {
	case HW_VERSION_82341C:
		if (assert)
			hp_priv->config_control_bits |= DONE_PGL_BIT;
		else
			hp_priv->config_control_bits &= ~DONE_PGL_BIT;
		outb(hp_priv->config_control_bits, hp_priv->iobase[0] + CONFIG_CONTROL_STATUS_REG);
		break;
	case HW_VERSION_82341D:
		if (assert)
			isapnp_write_byte(PIO_DATA_REG, HP_82341D_NOT_PROG_BIT);
		else
			isapnp_write_byte(PIO_DATA_REG, 0x0);
		break;
	default:
		break;
	}
}

// clear xilinx firmware
static int clear_xilinx(struct hp_82341_priv *hp_priv)
{
	set_xilinx_not_prog(hp_priv, 1);
	if (msleep_interruptible(1))
		return -EINTR;
	set_xilinx_not_prog(hp_priv, 0);
	if (msleep_interruptible(1))
		return -EINTR;
	set_xilinx_not_prog(hp_priv, 1);
	if (msleep_interruptible(1))
		return -EINTR;
	return 0;
}

int hp_82341_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct hp_82341_priv *hp_priv;
	struct tms9914_priv *tms_priv;
	u32 start_addr;
	u32 iobase;
	int irq;
	int i;
	int retval;

	board->status = 0;
	if (hp_82341_allocate_private(board))
		return -ENOMEM;
	hp_priv = board->private_data;
	tms_priv = &hp_priv->tms9914_priv;
	tms_priv->read_byte = hp_82341_read_byte;
	tms_priv->write_byte = hp_82341_write_byte;
	tms_priv->offset = 1;

	if (config->ibbase == 0) {
		struct pnp_dev *dev;
		int retval = hp_82341_find_isapnp_board(&dev);

		if (retval < 0)
			return retval;
		hp_priv->pnp_dev = dev;
		iobase = pnp_port_start(dev, 0);
		irq = pnp_irq(dev, 0);
		hp_priv->hw_version = HW_VERSION_82341D;
		hp_priv->io_region_offset = 0x8;
	} else {
		iobase = config->ibbase;
		irq = config->ibirq;
		hp_priv->hw_version = HW_VERSION_82341C;
		hp_priv->io_region_offset = 0x400;
	}
	pr_info("hp_82341: base io 0x%u\n", iobase);
	for (i = 0; i < hp_82341_num_io_regions; ++i) {
		start_addr = iobase + i * hp_priv->io_region_offset;
		if (!request_region(start_addr, hp_82341_region_iosize, "hp_82341")) {
			pr_err("hp_82341: failed to allocate io ports 0x%lx-0x%lx\n",
			       start_addr,
			       start_addr + hp_82341_region_iosize - 1);
			return -EIO;
		}
		hp_priv->iobase[i] = start_addr;
	}
	tms_priv->iobase = hp_priv->iobase[2];
	if (hp_priv->hw_version == HW_VERSION_82341D) {
		retval = isapnp_cfg_begin(hp_priv->pnp_dev->card->number,
					  hp_priv->pnp_dev->number);
		if (retval < 0)	{
			pr_err("hp_82341: isapnp_cfg_begin returned error\n");
			return retval;
		}
		isapnp_write_byte(PIO_DIRECTION_REG, HP_82341D_XILINX_READY_BIT |
				  HP_82341D_XILINX_DONE_BIT);
	}
	retval = clear_xilinx(hp_priv);
	if (retval < 0)
		return retval;
	retval = hp_82341_load_firmware(hp_priv, config);
	if (hp_priv->hw_version == HW_VERSION_82341D)
		isapnp_cfg_end();
	if (retval < 0)
		return retval;
	if (irq_valid(hp_priv, irq) == 0)
		return -EINVAL;
	if (request_irq(irq, hp_82341_interrupt, 0, "hp_82341", board))	{
		pr_err("hp_82341: failed to allocate IRQ %d\n", irq);
		return -EIO;
	}
	hp_priv->irq = irq;
	pr_info("hp_82341: IRQ %d\n", irq);
	hp_priv->config_control_bits &= ~IRQ_SELECT_MASK;
	hp_priv->config_control_bits |= IRQ_SELECT_BITS(irq);
	outb(hp_priv->config_control_bits, hp_priv->iobase[0] + CONFIG_CONTROL_STATUS_REG);
	hp_priv->mode_control_bits |= ENABLE_IRQ_CONFIG_BIT;
	outb(hp_priv->mode_control_bits, hp_priv->iobase[0] + MODE_CONTROL_STATUS_REG);
	tms9914_board_reset(tms_priv);
	outb(ENABLE_BUFFER_END_EVENT_BIT | ENABLE_TERMINAL_COUNT_EVENT_BIT |
	     ENABLE_TI_INTERRUPT_EVENT_BIT, hp_priv->iobase[0] +  EVENT_ENABLE_REG);
	outb(ENABLE_BUFFER_END_INTERRUPT_BIT | ENABLE_TERMINAL_COUNT_INTERRUPT_BIT |
	     ENABLE_TI_INTERRUPT_BIT, hp_priv->iobase[0] + INTERRUPT_ENABLE_REG);
	//write clear event register
	outb((TI_INTERRUPT_EVENT_BIT | POINTERS_EQUAL_EVENT_BIT |
	      BUFFER_END_EVENT_BIT | TERMINAL_COUNT_EVENT_BIT),
	     hp_priv->iobase[0] + EVENT_STATUS_REG);

	tms9914_online(board, tms_priv);
	pr_info("hp_82341: board id %x %x %x %x\n", inb(hp_priv->iobase[1] + ID0_REG),
		inb(hp_priv->iobase[1] + ID1_REG), inb(hp_priv->iobase[2] + ID2_REG),
		inb(hp_priv->iobase[2] + ID3_REG));
	return 0;
}

void hp_82341_detach(gpib_board_t *board)
{
	struct hp_82341_priv *hp_priv = board->private_data;
	struct tms9914_priv *tms_priv;
	int i;

	if (hp_priv) {
		tms_priv = &hp_priv->tms9914_priv;
		if (hp_priv->iobase[0])	{
			outb(0, hp_priv->iobase[0] + INTERRUPT_ENABLE_REG);
			if (tms_priv->iobase)
				tms9914_board_reset(tms_priv);
			if (hp_priv->irq)
				free_irq(hp_priv->irq, board);
		}
		for (i = 0; i < hp_82341_num_io_regions; ++i) {
			if (hp_priv->iobase[i])
				release_region(hp_priv->iobase[i], hp_82341_region_iosize);
		}
		if (hp_priv->pnp_dev)
			pnp_device_detach(hp_priv->pnp_dev);
	}
	hp_82341_free_private(board);
}

static const struct pnp_device_id hp_82341_pnp_table[] = {
	{.id = "HWP1411"},
	{.id = ""}
};
MODULE_DEVICE_TABLE(pnp, hp_82341_pnp_table);

static int __init hp_82341_init_module(void)
{
	int ret;

	ret = gpib_register_driver(&hp_82341_unaccel_interface, THIS_MODULE);
	if (ret) {
		pr_err("hp_82341: gpib_register_driver failed: error = %d\n", ret);
		return ret;
	}

	ret = gpib_register_driver(&hp_82341_interface, THIS_MODULE);
	if (ret) {
		pr_err("hp_82341: gpib_register_driver failed: error = %d\n", ret);
		gpib_unregister_driver(&hp_82341_unaccel_interface);
		return ret;
	}

	return 0;
}

static void __exit hp_82341_exit_module(void)
{
	gpib_unregister_driver(&hp_82341_interface);
	gpib_unregister_driver(&hp_82341_unaccel_interface);
}

module_init(hp_82341_init_module);
module_exit(hp_82341_exit_module);

/*
 * GPIB interrupt service routines
 */
unsigned short read_and_clear_event_status(gpib_board_t *board)
{
	struct hp_82341_priv *hp_priv = board->private_data;
	unsigned long flags;
	unsigned short status;

	spin_lock_irqsave(&board->spinlock, flags);
	status = hp_priv->event_status_bits;
	hp_priv->event_status_bits = 0;
	spin_unlock_irqrestore(&board->spinlock, flags);
	return status;
}

irqreturn_t hp_82341_interrupt(int irq, void *arg)
{
	int status1, status2;
	gpib_board_t *board = arg;
	struct hp_82341_priv *hp_priv = board->private_data;
	struct tms9914_priv *tms_priv = &hp_priv->tms9914_priv;
	unsigned long flags;
	irqreturn_t retval = IRQ_NONE;
	int event_status;

	spin_lock_irqsave(&board->spinlock, flags);
	event_status = inb(hp_priv->iobase[0] + EVENT_STATUS_REG);
//	printk("hp_82341: interrupt event_status=0x%x\n", event_status);
	if (event_status & INTERRUPT_PENDING_EVENT_BIT)
		retval = IRQ_HANDLED;
	//write-clear status bits
	if (event_status & (TI_INTERRUPT_EVENT_BIT | POINTERS_EQUAL_EVENT_BIT |
			    BUFFER_END_EVENT_BIT | TERMINAL_COUNT_EVENT_BIT)) {
		outb(event_status & (TI_INTERRUPT_EVENT_BIT | POINTERS_EQUAL_EVENT_BIT |
				     BUFFER_END_EVENT_BIT | TERMINAL_COUNT_EVENT_BIT),
		     hp_priv->iobase[0] + EVENT_STATUS_REG);
		hp_priv->event_status_bits |= event_status;
	}
	if (event_status & TI_INTERRUPT_EVENT_BIT) {
		status1 = read_byte(tms_priv, ISR0);
		status2 = read_byte(tms_priv, ISR1);
		tms9914_interrupt_have_status(board, tms_priv, status1, status2);
/*		printk("hp_82341: interrupt status1=0x%x status2=0x%x\n",
 *			status1, status2);
 */
	}
	spin_unlock_irqrestore(&board->spinlock, flags);
	return retval;
}

int read_transfer_counter(struct hp_82341_priv *hp_priv)
{
	int lo, mid, value;

	lo = inb(hp_priv->iobase[1] + TRANSFER_COUNT_LOW_REG);
	mid = inb(hp_priv->iobase[1] + TRANSFER_COUNT_MID_REG);
	value = (lo & 0xff) | ((mid << 8) & 0x7f00);
	value = ~(value - 1) & 0x7fff;
	return value;
}

void set_transfer_counter(struct hp_82341_priv *hp_priv, int count)
{
	int complement = -count;

	outb(complement & 0xff, hp_priv->iobase[1] + TRANSFER_COUNT_LOW_REG);
	outb((complement >> 8) & 0xff, hp_priv->iobase[1] + TRANSFER_COUNT_MID_REG);
	//I don't think the hi count reg is even used, but oh well
	outb((complement >> 16) & 0xf, hp_priv->iobase[1] + TRANSFER_COUNT_HIGH_REG);
}

