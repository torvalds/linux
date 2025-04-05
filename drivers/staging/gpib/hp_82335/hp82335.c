// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 * copyright            : (C) 2002 by Frank Mori Hess                      *
 ***************************************************************************/

/*should enable ATN interrupts (and update board->status on occurrence),
 *	implement recovery from bus errors (if necessary)
 */

#include "hp82335.h"
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver for HP 82335 interface cards");

static int hp82335_attach(gpib_board_t *board, const gpib_board_config_t *config);

static void hp82335_detach(gpib_board_t *board);

// wrappers for interface functions
int hp82335_read(gpib_board_t *board, uint8_t *buffer, size_t length, int *end, size_t *bytes_read)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_read(board, &priv->tms9914_priv, buffer, length, end, bytes_read);
}

int hp82335_write(gpib_board_t *board, uint8_t *buffer, size_t length, int send_eoi,
		  size_t *bytes_written)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_write(board, &priv->tms9914_priv, buffer, length, send_eoi, bytes_written);
}

int hp82335_command(gpib_board_t *board, uint8_t *buffer, size_t length, size_t *bytes_written)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_command(board, &priv->tms9914_priv, buffer, length, bytes_written);
}

int hp82335_take_control(gpib_board_t *board, int synchronous)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_take_control(board, &priv->tms9914_priv, synchronous);
}

int hp82335_go_to_standby(gpib_board_t *board)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_go_to_standby(board, &priv->tms9914_priv);
}

void hp82335_request_system_control(gpib_board_t *board, int request_control)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_request_system_control(board, &priv->tms9914_priv, request_control);
}

void hp82335_interface_clear(gpib_board_t *board, int assert)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_interface_clear(board, &priv->tms9914_priv, assert);
}

void hp82335_remote_enable(gpib_board_t *board, int enable)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_remote_enable(board, &priv->tms9914_priv, enable);
}

int hp82335_enable_eos(gpib_board_t *board, uint8_t eos_byte, int compare_8_bits)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_enable_eos(board, &priv->tms9914_priv, eos_byte, compare_8_bits);
}

void hp82335_disable_eos(gpib_board_t *board)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_disable_eos(board, &priv->tms9914_priv);
}

unsigned int hp82335_update_status(gpib_board_t *board, unsigned int clear_mask)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_update_status(board, &priv->tms9914_priv, clear_mask);
}

int hp82335_primary_address(gpib_board_t *board, unsigned int address)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_primary_address(board, &priv->tms9914_priv, address);
}

int hp82335_secondary_address(gpib_board_t *board, unsigned int address, int enable)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_secondary_address(board, &priv->tms9914_priv, address, enable);
}

int hp82335_parallel_poll(gpib_board_t *board, uint8_t *result)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_parallel_poll(board, &priv->tms9914_priv, result);
}

void hp82335_parallel_poll_configure(gpib_board_t *board, uint8_t config)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_parallel_poll_configure(board, &priv->tms9914_priv, config);
}

void hp82335_parallel_poll_response(gpib_board_t *board, int ist)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_parallel_poll_response(board, &priv->tms9914_priv, ist);
}

void hp82335_serial_poll_response(gpib_board_t *board, uint8_t status)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_serial_poll_response(board, &priv->tms9914_priv, status);
}

static uint8_t hp82335_serial_poll_status(gpib_board_t *board)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_serial_poll_status(board, &priv->tms9914_priv);
}

static int hp82335_line_status(const gpib_board_t *board)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_line_status(board, &priv->tms9914_priv);
}

static unsigned int hp82335_t1_delay(gpib_board_t *board, unsigned int nano_sec)
{
	struct hp82335_priv *priv = board->private_data;

	return tms9914_t1_delay(board, &priv->tms9914_priv, nano_sec);
}

void hp82335_return_to_local(gpib_board_t *board)
{
	struct hp82335_priv *priv = board->private_data;

	tms9914_return_to_local(board, &priv->tms9914_priv);
}

static gpib_interface_t hp82335_interface = {
	.name = "hp82335",
	.attach = hp82335_attach,
	.detach = hp82335_detach,
	.read = hp82335_read,
	.write = hp82335_write,
	.command = hp82335_command,
	.request_system_control = hp82335_request_system_control,
	.take_control = hp82335_take_control,
	.go_to_standby = hp82335_go_to_standby,
	.interface_clear = hp82335_interface_clear,
	.remote_enable = hp82335_remote_enable,
	.enable_eos = hp82335_enable_eos,
	.disable_eos = hp82335_disable_eos,
	.parallel_poll = hp82335_parallel_poll,
	.parallel_poll_configure = hp82335_parallel_poll_configure,
	.parallel_poll_response = hp82335_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = hp82335_line_status,
	.update_status = hp82335_update_status,
	.primary_address = hp82335_primary_address,
	.secondary_address = hp82335_secondary_address,
	.serial_poll_response = hp82335_serial_poll_response,
	.serial_poll_status = hp82335_serial_poll_status,
	.t1_delay = hp82335_t1_delay,
	.return_to_local = hp82335_return_to_local,
};

int hp82335_allocate_private(gpib_board_t *board)
{
	board->private_data = kzalloc(sizeof(struct hp82335_priv), GFP_KERNEL);
	if (!board->private_data)
		return -1;
	return 0;
}

void hp82335_free_private(gpib_board_t *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

static inline unsigned int tms9914_to_hp82335_offset(unsigned int register_num)
{
	return 0x1ff8 + register_num;
}

static uint8_t hp82335_read_byte(struct tms9914_priv *priv, unsigned int register_num)
{
	return tms9914_iomem_read_byte(priv, tms9914_to_hp82335_offset(register_num));
}

static void hp82335_write_byte(struct tms9914_priv *priv, uint8_t data, unsigned int register_num)
{
	tms9914_iomem_write_byte(priv, data, tms9914_to_hp82335_offset(register_num));
}

static void hp82335_clear_interrupt(struct hp82335_priv *hp_priv)
{
	struct tms9914_priv *tms_priv = &hp_priv->tms9914_priv;

	writeb(0, tms_priv->mmiobase + HPREG_INTR_CLEAR);
}

int hp82335_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	struct hp82335_priv *hp_priv;
	struct tms9914_priv *tms_priv;
	int retval;
	const unsigned long upper_iomem_base = config->ibbase + hp82335_rom_size;

	board->status = 0;

	if (hp82335_allocate_private(board))
		return -ENOMEM;
	hp_priv = board->private_data;
	tms_priv = &hp_priv->tms9914_priv;
	tms_priv->read_byte = hp82335_read_byte;
	tms_priv->write_byte = hp82335_write_byte;
	tms_priv->offset = 1;

	switch (config->ibbase) {
	case 0xc4000:
	case 0xc8000:
	case 0xcc000:
	case 0xd0000:
	case 0xd4000:
	case 0xd8000:
	case 0xdc000:
	case 0xe0000:
	case 0xe4000:
	case 0xe8000:
	case 0xec000:
	case 0xf0000:
	case 0xf4000:
	case 0xf8000:
	case 0xfc000:
		break;
	default:
		pr_err("hp82335: invalid base io address 0x%u\n", config->ibbase);
		return -EINVAL;
	}
	if (!request_mem_region(upper_iomem_base, hp82335_upper_iomem_size, "hp82335")) {
		pr_err("hp82335: failed to allocate io memory region 0x%lx-0x%lx\n",
		       upper_iomem_base, upper_iomem_base + hp82335_upper_iomem_size - 1);
		return -EBUSY;
	}
	hp_priv->raw_iobase = upper_iomem_base;
	tms_priv->mmiobase = ioremap(upper_iomem_base, hp82335_upper_iomem_size);
	pr_info("hp82335: upper half of 82335 iomem region 0x%lx remapped to 0x%p\n",
		hp_priv->raw_iobase, tms_priv->mmiobase);

	retval = request_irq(config->ibirq, hp82335_interrupt, 0, "hp82335", board);
	if (retval) {
		pr_err("hp82335: can't request IRQ %d\n", config->ibirq);
		return retval;
	}
	hp_priv->irq = config->ibirq;
	pr_info("hp82335: IRQ %d\n", config->ibirq);

	tms9914_board_reset(tms_priv);

	hp82335_clear_interrupt(hp_priv);

	writeb(INTR_ENABLE, tms_priv->mmiobase + HPREG_CCR);

	tms9914_online(board, tms_priv);

	return 0;
}

void hp82335_detach(gpib_board_t *board)
{
	struct hp82335_priv *hp_priv = board->private_data;
	struct tms9914_priv *tms_priv;

	if (hp_priv) {
		tms_priv = &hp_priv->tms9914_priv;
		if (hp_priv->irq)
			free_irq(hp_priv->irq, board);
		if (tms_priv->mmiobase) {
			writeb(0, tms_priv->mmiobase + HPREG_CCR);
			tms9914_board_reset(tms_priv);
			iounmap(tms_priv->mmiobase);
		}
		if (hp_priv->raw_iobase)
			release_mem_region(hp_priv->raw_iobase, hp82335_upper_iomem_size);
	}
	hp82335_free_private(board);
}

static int __init hp82335_init_module(void)
{
	int result = gpib_register_driver(&hp82335_interface, THIS_MODULE);

	if (result) {
		pr_err("hp82335: gpib_register_driver failed: error = %d\n", result);
		return result;
	}

	return 0;
}

static void __exit hp82335_exit_module(void)
{
	gpib_unregister_driver(&hp82335_interface);
}

module_init(hp82335_init_module);
module_exit(hp82335_exit_module);

/*
 * GPIB interrupt service routines
 */

irqreturn_t hp82335_interrupt(int irq, void *arg)
{
	int status1, status2;
	gpib_board_t *board = arg;
	struct hp82335_priv *priv = board->private_data;
	unsigned long flags;
	irqreturn_t retval;

	spin_lock_irqsave(&board->spinlock, flags);
	status1 = read_byte(&priv->tms9914_priv, ISR0);
	status2 = read_byte(&priv->tms9914_priv, ISR1);
	hp82335_clear_interrupt(priv);
	retval = tms9914_interrupt_have_status(board, &priv->tms9914_priv, status1, status2);
	spin_unlock_irqrestore(&board->spinlock, flags);
	return retval;
}

