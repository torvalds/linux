// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (c) 2001 Micro Solutions Inc.
 *
 * backpack.c is a low-level protocol driver for the Micro Solutions
 * "BACKPACK" parallel port IDE adapter (works on Series 6 drives).
 *
 * Written by: Ken Hahn (linux-dev@micro-solutions.com)
 *             Clive Turvey (linux-dev@micro-solutions.com)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/parport.h>
#include "pata_parport.h"

/* 60772 Commands */
#define ACCESS_REG		0x00
#define ACCESS_PORT		0x40

#define ACCESS_READ		0x00
#define ACCESS_WRITE		0x20

/* 60772 Command Prefix */
#define CMD_PREFIX_SET		0xe0	// Special command that modifies next command's operation
#define CMD_PREFIX_RESET	0xc0	// Resets current cmd modifier reg bits
 #define PREFIX_IO16		0x01	// perform 16-bit wide I/O
 #define PREFIX_FASTWR		0x04	// enable PPC mode fast-write
 #define PREFIX_BLK		0x08	// enable block transfer mode

/* 60772 Registers */
#define REG_STATUS		0x00	// status register
 #define STATUS_IRQA		0x01	// Peripheral IRQA line
 #define STATUS_EEPROM_DO	0x40	// Serial EEPROM data bit
#define REG_VERSION		0x01	// PPC version register (read)
#define REG_HWCFG		0x02	// Hardware Config register
#define REG_RAMSIZE		0x03	// Size of RAM Buffer
 #define RAMSIZE_128K		0x02
#define REG_EEPROM		0x06	// EEPROM control register
 #define EEPROM_SK		0x01	// eeprom SK bit
 #define EEPROM_DI		0x02	// eeprom DI bit
 #define EEPROM_CS		0x04	// eeprom CS bit
 #define EEPROM_EN		0x08	// eeprom output enable
#define REG_BLKSIZE		0x08	// Block transfer len (24 bit)

/* flags */
#define fifo_wait		0x10

/* DONT CHANGE THESE LEST YOU BREAK EVERYTHING - BIT FIELD DEPENDENCIES */
#define PPCMODE_UNI_SW		0
#define PPCMODE_UNI_FW		1
#define PPCMODE_BI_SW		2
#define PPCMODE_BI_FW		3
#define PPCMODE_EPP_BYTE	4
#define PPCMODE_EPP_WORD	5
#define PPCMODE_EPP_DWORD	6

static int mode_map[] = { PPCMODE_UNI_FW, PPCMODE_BI_FW, PPCMODE_EPP_BYTE,
			  PPCMODE_EPP_WORD, PPCMODE_EPP_DWORD };

static void bpck6_send_cmd(struct pi_adapter *pi, u8 cmd)
{
	switch (mode_map[pi->mode]) {
	case PPCMODE_UNI_SW:
	case PPCMODE_UNI_FW:
	case PPCMODE_BI_SW:
	case PPCMODE_BI_FW:
		parport_write_data(pi->pardev->port, cmd);
		parport_frob_control(pi->pardev->port, 0, PARPORT_CONTROL_AUTOFD);
		break;
	case PPCMODE_EPP_BYTE:
	case PPCMODE_EPP_WORD:
	case PPCMODE_EPP_DWORD:
		pi->pardev->port->ops->epp_write_addr(pi->pardev->port, &cmd, 1, 0);
		break;
	}
}

static u8 bpck6_rd_data_byte(struct pi_adapter *pi)
{
	u8 data = 0;

	switch (mode_map[pi->mode]) {
	case PPCMODE_UNI_SW:
	case PPCMODE_UNI_FW:
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
							PARPORT_CONTROL_INIT);
		data = parport_read_status(pi->pardev->port);
		data = ((data & 0x80) >> 1) | ((data & 0x38) >> 3);
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
							PARPORT_CONTROL_STROBE);
		data |= parport_read_status(pi->pardev->port) & 0xB8;
		break;
	case PPCMODE_BI_SW:
	case PPCMODE_BI_FW:
		parport_data_reverse(pi->pardev->port);
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
				PARPORT_CONTROL_STROBE | PARPORT_CONTROL_INIT);
		data = parport_read_data(pi->pardev->port);
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE, 0);
		parport_data_forward(pi->pardev->port);
		break;
	case PPCMODE_EPP_BYTE:
	case PPCMODE_EPP_WORD:
	case PPCMODE_EPP_DWORD:
		pi->pardev->port->ops->epp_read_data(pi->pardev->port, &data, 1, 0);
		break;
	}

	return data;
}

static void bpck6_wr_data_byte(struct pi_adapter *pi, u8 data)
{
	switch (mode_map[pi->mode]) {
	case PPCMODE_UNI_SW:
	case PPCMODE_UNI_FW:
	case PPCMODE_BI_SW:
	case PPCMODE_BI_FW:
		parport_write_data(pi->pardev->port, data);
		parport_frob_control(pi->pardev->port, 0, PARPORT_CONTROL_INIT);
		break;
	case PPCMODE_EPP_BYTE:
	case PPCMODE_EPP_WORD:
	case PPCMODE_EPP_DWORD:
		pi->pardev->port->ops->epp_write_data(pi->pardev->port, &data, 1, 0);
		break;
	}
}

static int bpck6_read_regr(struct pi_adapter *pi, int cont, int reg)
{
	u8 port = cont ? reg | 8 : reg;

	bpck6_send_cmd(pi, port | ACCESS_PORT | ACCESS_READ);
	return bpck6_rd_data_byte(pi);
}

static void bpck6_write_regr(struct pi_adapter *pi, int cont, int reg, int val)
{
	u8 port = cont ? reg | 8 : reg;

	bpck6_send_cmd(pi, port | ACCESS_PORT | ACCESS_WRITE);
	bpck6_wr_data_byte(pi, val);
}

static void bpck6_wait_for_fifo(struct pi_adapter *pi)
{
	int i;

	if (pi->private & fifo_wait) {
		for (i = 0; i < 20; i++)
			parport_read_status(pi->pardev->port);
	}
}

static void bpck6_write_block(struct pi_adapter *pi, char *buf, int len)
{
	u8 this, last;

	bpck6_send_cmd(pi, REG_BLKSIZE | ACCESS_REG | ACCESS_WRITE);
	bpck6_wr_data_byte(pi, (u8)len);
	bpck6_wr_data_byte(pi, (u8)(len >> 8));
	bpck6_wr_data_byte(pi, 0);

	bpck6_send_cmd(pi, CMD_PREFIX_SET | PREFIX_IO16 | PREFIX_BLK);
	bpck6_send_cmd(pi, ATA_REG_DATA | ACCESS_PORT | ACCESS_WRITE);

	switch (mode_map[pi->mode]) {
	case PPCMODE_UNI_SW:
	case PPCMODE_BI_SW:
		while (len--) {
			parport_write_data(pi->pardev->port, *buf++);
			parport_frob_control(pi->pardev->port, 0,
							PARPORT_CONTROL_INIT);
		}
		break;
	case PPCMODE_UNI_FW:
	case PPCMODE_BI_FW:
		bpck6_send_cmd(pi, CMD_PREFIX_SET | PREFIX_FASTWR);

		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
							PARPORT_CONTROL_STROBE);

		last = *buf;

		parport_write_data(pi->pardev->port, last);

		while (len) {
			this = *buf++;
			len--;

			if (this == last) {
				parport_frob_control(pi->pardev->port, 0,
							PARPORT_CONTROL_INIT);
			} else {
				parport_write_data(pi->pardev->port, this);
				last = this;
			}
		}

		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
							0);
		bpck6_send_cmd(pi, CMD_PREFIX_RESET | PREFIX_FASTWR);
		break;
	case PPCMODE_EPP_BYTE:
		pi->pardev->port->ops->epp_write_data(pi->pardev->port, buf,
						len, PARPORT_EPP_FAST_8);
		bpck6_wait_for_fifo(pi);
		break;
	case PPCMODE_EPP_WORD:
		pi->pardev->port->ops->epp_write_data(pi->pardev->port, buf,
						len, PARPORT_EPP_FAST_16);
		bpck6_wait_for_fifo(pi);
		break;
	case PPCMODE_EPP_DWORD:
		pi->pardev->port->ops->epp_write_data(pi->pardev->port, buf,
						len, PARPORT_EPP_FAST_32);
		bpck6_wait_for_fifo(pi);
		break;
	}

	bpck6_send_cmd(pi, CMD_PREFIX_RESET | PREFIX_IO16 | PREFIX_BLK);
}

static void bpck6_read_block(struct pi_adapter *pi, char *buf, int len)
{
	bpck6_send_cmd(pi, REG_BLKSIZE | ACCESS_REG | ACCESS_WRITE);
	bpck6_wr_data_byte(pi, (u8)len);
	bpck6_wr_data_byte(pi, (u8)(len >> 8));
	bpck6_wr_data_byte(pi, 0);

	bpck6_send_cmd(pi, CMD_PREFIX_SET | PREFIX_IO16 | PREFIX_BLK);
	bpck6_send_cmd(pi, ATA_REG_DATA | ACCESS_PORT | ACCESS_READ);

	switch (mode_map[pi->mode]) {
	case PPCMODE_UNI_SW:
	case PPCMODE_UNI_FW:
		while (len) {
			u8 d;

			parport_frob_control(pi->pardev->port,
					PARPORT_CONTROL_STROBE,
					PARPORT_CONTROL_INIT); /* DATA STROBE */
			d = parport_read_status(pi->pardev->port);
			d = ((d & 0x80) >> 1) | ((d & 0x38) >> 3);
			parport_frob_control(pi->pardev->port,
					PARPORT_CONTROL_STROBE,
					PARPORT_CONTROL_STROBE);
			d |= parport_read_status(pi->pardev->port) & 0xB8;
			*buf++ = d;
			len--;
		}
		break;
	case PPCMODE_BI_SW:
	case PPCMODE_BI_FW:
		parport_data_reverse(pi->pardev->port);
		while (len) {
			parport_frob_control(pi->pardev->port,
				PARPORT_CONTROL_STROBE,
				PARPORT_CONTROL_STROBE | PARPORT_CONTROL_INIT);
			*buf++ = parport_read_data(pi->pardev->port);
			len--;
		}
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
					0);
		parport_data_forward(pi->pardev->port);
		break;
	case PPCMODE_EPP_BYTE:
		pi->pardev->port->ops->epp_read_data(pi->pardev->port, buf, len,
						PARPORT_EPP_FAST_8);
		break;
	case PPCMODE_EPP_WORD:
		pi->pardev->port->ops->epp_read_data(pi->pardev->port, buf, len,
						PARPORT_EPP_FAST_16);
		break;
	case PPCMODE_EPP_DWORD:
		pi->pardev->port->ops->epp_read_data(pi->pardev->port, buf, len,
						PARPORT_EPP_FAST_32);
		break;
	}

	bpck6_send_cmd(pi, CMD_PREFIX_RESET | PREFIX_IO16 | PREFIX_BLK);
}

static int bpck6_open(struct pi_adapter *pi)
{
	u8 i, j, k;

	pi->saved_r0 = parport_read_data(pi->pardev->port);
	pi->saved_r2 = parport_read_control(pi->pardev->port) & 0x5F;

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT,
						PARPORT_CONTROL_SELECT);
	if (pi->saved_r0 == 'b')
		parport_write_data(pi->pardev->port, 'x');
	parport_write_data(pi->pardev->port, 'b');
	parport_write_data(pi->pardev->port, 'p');
	parport_write_data(pi->pardev->port, pi->unit);
	parport_write_data(pi->pardev->port, ~pi->unit);

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT, 0);
	parport_write_control(pi->pardev->port, PARPORT_CONTROL_INIT);

	i = mode_map[pi->mode] & 0x0C;
	if (i == 0)
		i = (mode_map[pi->mode] & 2) | 1;
	parport_write_data(pi->pardev->port, i);

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT,
						PARPORT_CONTROL_SELECT);
	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_AUTOFD,
						PARPORT_CONTROL_AUTOFD);

	j = ((i & 0x08) << 4) | ((i & 0x07) << 3);
	k = parport_read_status(pi->pardev->port) & 0xB8;
	if (j != k)
		goto fail;

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_AUTOFD, 0);
	k = (parport_read_status(pi->pardev->port) & 0xB8) ^ 0xB8;
	if (j != k)
		goto fail;

	if (i & 4) {
		/* EPP */
		parport_frob_control(pi->pardev->port,
			PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT, 0);
	} else {
		/* PPC/ECP */
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT, 0);
	}

	pi->private = 0;

	bpck6_send_cmd(pi, ACCESS_REG | ACCESS_WRITE | REG_RAMSIZE);
	bpck6_wr_data_byte(pi, RAMSIZE_128K);

	bpck6_send_cmd(pi, ACCESS_REG | ACCESS_READ | REG_VERSION);
	if ((bpck6_rd_data_byte(pi) & 0x3F) == 0x0C)
		pi->private |= fifo_wait;

	return 1;

fail:
	parport_write_control(pi->pardev->port, pi->saved_r2);
	parport_write_data(pi->pardev->port, pi->saved_r0);

	return 0;
}

static void bpck6_deselect(struct pi_adapter *pi)
{
	if (mode_map[pi->mode] & 4) {
		/* EPP */
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_INIT,
				     PARPORT_CONTROL_INIT);
	} else {
		/* PPC/ECP */
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT,
				     PARPORT_CONTROL_SELECT);
	}

	parport_write_data(pi->pardev->port, pi->saved_r0);
	parport_write_control(pi->pardev->port,
			pi->saved_r2 | PARPORT_CONTROL_SELECT);
	parport_write_control(pi->pardev->port, pi->saved_r2);
}

static void bpck6_wr_extout(struct pi_adapter *pi, u8 regdata)
{
	bpck6_send_cmd(pi, REG_VERSION | ACCESS_REG | ACCESS_WRITE);
	bpck6_wr_data_byte(pi, (u8)((regdata & 0x03) << 6));
}

static void bpck6_connect(struct pi_adapter *pi)
{
	dev_dbg(&pi->dev, "connect\n");

	bpck6_open(pi);
	bpck6_wr_extout(pi, 0x3);
}

static void bpck6_disconnect(struct pi_adapter *pi)
{
	dev_dbg(&pi->dev, "disconnect\n");
	bpck6_wr_extout(pi, 0x0);
	bpck6_deselect(pi);
}

/* check for 8-bit port */
static int bpck6_test_port(struct pi_adapter *pi)
{
	dev_dbg(&pi->dev, "PARPORT indicates modes=%x for lp=0x%lx\n",
		pi->pardev->port->modes, pi->pardev->port->base);

	/* look at the parport device to see what modes we can use */
	if (pi->pardev->port->modes & PARPORT_MODE_EPP)
		return 5; /* Can do EPP */
	if (pi->pardev->port->modes & PARPORT_MODE_TRISTATE)
		return 2;
	return 1; /* Just flat SPP */
}

static int bpck6_probe_unit(struct pi_adapter *pi)
{
	int out, saved_mode;

	dev_dbg(&pi->dev, "PROBE UNIT %x on port:%x\n", pi->unit, pi->port);

	saved_mode = pi->mode;
	/*LOWER DOWN TO UNIDIRECTIONAL*/
	pi->mode = 0;

	out = bpck6_open(pi);

	dev_dbg(&pi->dev, "ppc_open returned %2x\n", out);

	if (out) {
		bpck6_deselect(pi);
		dev_dbg(&pi->dev, "leaving probe\n");
		pi->mode = saved_mode;
		return 1;
	}

	dev_dbg(&pi->dev, "Failed open\n");
	pi->mode = saved_mode;

	return 0;
}

static void bpck6_log_adapter(struct pi_adapter *pi)
{
	char *mode_string[5] = { "4-bit", "8-bit", "EPP-8", "EPP-16", "EPP-32" };

	dev_info(&pi->dev,
		 "Micro Solutions BACKPACK Drive unit %d at 0x%x, mode:%d (%s), delay %d\n",
		 pi->unit, pi->port, pi->mode, mode_string[pi->mode], pi->delay);
}

static struct pi_protocol bpck6 = {
	.owner		= THIS_MODULE,
	.name		= "bpck6",
	.max_mode	= 5,
	.epp_first	= 2, /* 2-5 use epp (need 8 ports) */
	.max_units	= 255,
	.write_regr	= bpck6_write_regr,
	.read_regr	= bpck6_read_regr,
	.write_block	= bpck6_write_block,
	.read_block	= bpck6_read_block,
	.connect	= bpck6_connect,
	.disconnect	= bpck6_disconnect,
	.test_port	= bpck6_test_port,
	.probe_unit	= bpck6_probe_unit,
	.log_adapter	= bpck6_log_adapter,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Micro Solutions Inc.");
MODULE_DESCRIPTION("BACKPACK Protocol module, compatible with PARIDE");
module_pata_parport_driver(bpck6);
