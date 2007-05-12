/*
 * Bootloader version of the i2c driver for the MV64x60.
 *
 * Author: Dale Farnsworth <dfarnsworth@mvista.com>
 * Maintained by: Mark A. Greer <mgreer@mvista.com>
 *
 * 2003, 2007 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program is
 * licensed "as is" without any warranty of any kind, whether express or
 * implied.
 */

#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "page.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "ops.h"
#include "mv64x60.h"

extern void udelay(long);

/* Register defines */
#define MV64x60_I2C_REG_SLAVE_ADDR			0x00
#define MV64x60_I2C_REG_DATA				0x04
#define MV64x60_I2C_REG_CONTROL				0x08
#define MV64x60_I2C_REG_STATUS				0x0c
#define MV64x60_I2C_REG_BAUD				0x0c
#define MV64x60_I2C_REG_EXT_SLAVE_ADDR			0x10
#define MV64x60_I2C_REG_SOFT_RESET			0x1c

#define MV64x60_I2C_CONTROL_ACK				0x04
#define MV64x60_I2C_CONTROL_IFLG			0x08
#define MV64x60_I2C_CONTROL_STOP			0x10
#define MV64x60_I2C_CONTROL_START			0x20
#define MV64x60_I2C_CONTROL_TWSIEN			0x40
#define MV64x60_I2C_CONTROL_INTEN			0x80

#define MV64x60_I2C_STATUS_BUS_ERR			0x00
#define MV64x60_I2C_STATUS_MAST_START			0x08
#define MV64x60_I2C_STATUS_MAST_REPEAT_START		0x10
#define MV64x60_I2C_STATUS_MAST_WR_ADDR_ACK		0x18
#define MV64x60_I2C_STATUS_MAST_WR_ADDR_NO_ACK		0x20
#define MV64x60_I2C_STATUS_MAST_WR_ACK			0x28
#define MV64x60_I2C_STATUS_MAST_WR_NO_ACK		0x30
#define MV64x60_I2C_STATUS_MAST_LOST_ARB		0x38
#define MV64x60_I2C_STATUS_MAST_RD_ADDR_ACK		0x40
#define MV64x60_I2C_STATUS_MAST_RD_ADDR_NO_ACK		0x48
#define MV64x60_I2C_STATUS_MAST_RD_DATA_ACK		0x50
#define MV64x60_I2C_STATUS_MAST_RD_DATA_NO_ACK		0x58
#define MV64x60_I2C_STATUS_MAST_WR_ADDR_2_ACK		0xd0
#define MV64x60_I2C_STATUS_MAST_WR_ADDR_2_NO_ACK	0xd8
#define MV64x60_I2C_STATUS_MAST_RD_ADDR_2_ACK		0xe0
#define MV64x60_I2C_STATUS_MAST_RD_ADDR_2_NO_ACK	0xe8
#define MV64x60_I2C_STATUS_NO_STATUS			0xf8

static u8 *ctlr_base;

static int mv64x60_i2c_wait_for_status(int wanted)
{
	int i;
	int status;

	for (i=0; i<1000; i++) {
		udelay(10);
		status = in_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_STATUS))
			& 0xff;
		if (status == wanted)
			return status;
	}
	return -status;
}

static int mv64x60_i2c_control(int control, int status)
{
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_CONTROL), control & 0xff);
	return mv64x60_i2c_wait_for_status(status);
}

static int mv64x60_i2c_read_byte(int control, int status)
{
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_CONTROL), control & 0xff);
	if (mv64x60_i2c_wait_for_status(status) < 0)
		return -1;
	return in_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_DATA)) & 0xff;
}

static int mv64x60_i2c_write_byte(int data, int control, int status)
{
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_DATA), data & 0xff);
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_CONTROL), control & 0xff);
	return mv64x60_i2c_wait_for_status(status);
}

int mv64x60_i2c_read(u32 devaddr, u8 *buf, u32 offset, u32 offset_size,
		 u32 count)
{
	int i;
	int data;
	int control;
	int status;

	if (ctlr_base == NULL)
		return -1;

	/* send reset */
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_SOFT_RESET), 0);
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_SLAVE_ADDR), 0);
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_EXT_SLAVE_ADDR), 0);
	out_le32((u32 *)(ctlr_base + MV64x60_I2C_REG_BAUD), (4 << 3) | 0x4);

	if (mv64x60_i2c_control(MV64x60_I2C_CONTROL_TWSIEN,
				MV64x60_I2C_STATUS_NO_STATUS) < 0)
		return -1;

	/* send start */
	control = MV64x60_I2C_CONTROL_START | MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_MAST_START;
	if (mv64x60_i2c_control(control, status) < 0)
		return -1;

	/* select device for writing */
	data = devaddr & ~0x1;
	control = MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_MAST_WR_ADDR_ACK;
	if (mv64x60_i2c_write_byte(data, control, status) < 0)
		return -1;

	/* send offset of data */
	control = MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_MAST_WR_ACK;
	if (offset_size > 1) {
		if (mv64x60_i2c_write_byte(offset >> 8, control, status) < 0)
			return -1;
	}
	if (mv64x60_i2c_write_byte(offset, control, status) < 0)
		return -1;

	/* resend start */
	control = MV64x60_I2C_CONTROL_START | MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_MAST_REPEAT_START;
	if (mv64x60_i2c_control(control, status) < 0)
		return -1;

	/* select device for reading */
	data = devaddr | 0x1;
	control = MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_MAST_RD_ADDR_ACK;
	if (mv64x60_i2c_write_byte(data, control, status) < 0)
		return -1;

	/* read all but last byte of data */
	control = MV64x60_I2C_CONTROL_ACK | MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_MAST_RD_DATA_ACK;

	for (i=1; i<count; i++) {
		data = mv64x60_i2c_read_byte(control, status);
		if (data < 0) {
			printf("errors on iteration %d\n", i);
			return -1;
		}
		*buf++ = data;
	}

	/* read last byte of data */
	control = MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_MAST_RD_DATA_NO_ACK;
	data = mv64x60_i2c_read_byte(control, status);
	if (data < 0)
		return -1;
	*buf++ = data;

	/* send stop */
	control = MV64x60_I2C_CONTROL_STOP | MV64x60_I2C_CONTROL_TWSIEN;
	status = MV64x60_I2C_STATUS_NO_STATUS;
	if (mv64x60_i2c_control(control, status) < 0)
		return -1;

	return count;
}

int mv64x60_i2c_open(void)
{
	u32 v;
	void *devp;

	devp = finddevice("/mv64x60/i2c");
	if (devp == NULL)
		goto err_out;
	if (getprop(devp, "virtual-reg", &v, sizeof(v)) != sizeof(v))
		goto err_out;

	ctlr_base = (u8 *)v;
	return 0;

err_out:
	return -1;
}

void mv64x60_i2c_close(void)
{
	ctlr_base = NULL;
}
