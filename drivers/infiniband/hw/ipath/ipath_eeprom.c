/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include "ipath_kernel.h"

/*
 * InfiniPath I2C driver for a serial eeprom.  This is not a generic
 * I2C interface.  For a start, the device we're using (Atmel AT24C11)
 * doesn't work like a regular I2C device.  It looks like one
 * electrically, but not logically.  Normal I2C devices have a single
 * 7-bit or 10-bit I2C address that they respond to.  Valid 7-bit
 * addresses range from 0x03 to 0x77.  Addresses 0x00 to 0x02 and 0x78
 * to 0x7F are special reserved addresses (e.g. 0x00 is the "general
 * call" address.)  The Atmel device, on the other hand, responds to ALL
 * 7-bit addresses.  It's designed to be the only device on a given I2C
 * bus.  A 7-bit address corresponds to the memory address within the
 * Atmel device itself.
 *
 * Also, the timing requirements mean more than simple software
 * bitbanging, with readbacks from chip to ensure timing (simple udelay
 * is not enough).
 *
 * This all means that accessing the device is specialized enough
 * that using the standard kernel I2C bitbanging interface would be
 * impossible.  For example, the core I2C eeprom driver expects to find
 * a device at one or more of a limited set of addresses only.  It doesn't
 * allow writing to an eeprom.  It also doesn't provide any means of
 * accessing eeprom contents from within the kernel, only via sysfs.
 */

enum i2c_type {
	i2c_line_scl = 0,
	i2c_line_sda
};

enum i2c_state {
	i2c_line_low = 0,
	i2c_line_high
};

#define READ_CMD 1
#define WRITE_CMD 0

static int eeprom_init;

/*
 * The gpioval manipulation really should be protected by spinlocks
 * or be converted to use atomic operations.
 */

/**
 * i2c_gpio_set - set a GPIO line
 * @dd: the infinipath device
 * @line: the line to set
 * @new_line_state: the state to set
 *
 * Returns 0 if the line was set to the new state successfully, non-zero
 * on error.
 */
static int i2c_gpio_set(struct ipath_devdata *dd,
			enum i2c_type line,
			enum i2c_state new_line_state)
{
	u64 read_val, write_val, mask, *gpioval;

	gpioval = &dd->ipath_gpio_out;
	read_val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_extctrl);
	if (line == i2c_line_scl)
		mask = dd->ipath_gpio_scl;
	else
		mask = dd->ipath_gpio_sda;

	if (new_line_state == i2c_line_high)
		/* tri-state the output rather than force high */
		write_val = read_val & ~mask;
	else
		/* config line to be an output */
		write_val = read_val | mask;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_extctrl, write_val);

	/* set high and verify */
	if (new_line_state == i2c_line_high)
		write_val = 0x1UL;
	else
		write_val = 0x0UL;

	if (line == i2c_line_scl) {
		write_val <<= dd->ipath_gpio_scl_num;
		*gpioval = *gpioval & ~(1UL << dd->ipath_gpio_scl_num);
		*gpioval |= write_val;
	} else {
		write_val <<= dd->ipath_gpio_sda_num;
		*gpioval = *gpioval & ~(1UL << dd->ipath_gpio_sda_num);
		*gpioval |= write_val;
	}
	ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_out, *gpioval);

	return 0;
}

/**
 * i2c_gpio_get - get a GPIO line state
 * @dd: the infinipath device
 * @line: the line to get
 * @curr_statep: where to put the line state
 *
 * Returns 0 if the line was set to the new state successfully, non-zero
 * on error.  curr_state is not set on error.
 */
static int i2c_gpio_get(struct ipath_devdata *dd,
			enum i2c_type line,
			enum i2c_state *curr_statep)
{
	u64 read_val, write_val, mask;
	int ret;

	/* check args */
	if (curr_statep == NULL) {
		ret = 1;
		goto bail;
	}

	read_val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_extctrl);
	/* config line to be an input */
	if (line == i2c_line_scl)
		mask = dd->ipath_gpio_scl;
	else
		mask = dd->ipath_gpio_sda;
	write_val = read_val & ~mask;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_extctrl, write_val);
	read_val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_extstatus);

	if (read_val & mask)
		*curr_statep = i2c_line_high;
	else
		*curr_statep = i2c_line_low;

	ret = 0;

bail:
	return ret;
}

/**
 * i2c_wait_for_writes - wait for a write
 * @dd: the infinipath device
 *
 * We use this instead of udelay directly, so we can make sure
 * that previous register writes have been flushed all the way
 * to the chip.  Since we are delaying anyway, the cost doesn't
 * hurt, and makes the bit twiddling more regular
 */
static void i2c_wait_for_writes(struct ipath_devdata *dd)
{
	(void)ipath_read_kreg32(dd, dd->ipath_kregs->kr_scratch);
	rmb();
}

static void scl_out(struct ipath_devdata *dd, u8 bit)
{
	i2c_gpio_set(dd, i2c_line_scl, bit ? i2c_line_high : i2c_line_low);

	i2c_wait_for_writes(dd);
}

static void sda_out(struct ipath_devdata *dd, u8 bit)
{
	i2c_gpio_set(dd, i2c_line_sda, bit ? i2c_line_high : i2c_line_low);

	i2c_wait_for_writes(dd);
}

static u8 sda_in(struct ipath_devdata *dd, int wait)
{
	enum i2c_state bit;

	if (i2c_gpio_get(dd, i2c_line_sda, &bit))
		ipath_dbg("get bit failed!\n");

	if (wait)
		i2c_wait_for_writes(dd);

	return bit == i2c_line_high ? 1U : 0;
}

/**
 * i2c_ackrcv - see if ack following write is true
 * @dd: the infinipath device
 */
static int i2c_ackrcv(struct ipath_devdata *dd)
{
	u8 ack_received;

	/* AT ENTRY SCL = LOW */
	/* change direction, ignore data */
	ack_received = sda_in(dd, 1);
	scl_out(dd, i2c_line_high);
	ack_received = sda_in(dd, 1) == 0;
	scl_out(dd, i2c_line_low);
	return ack_received;
}

/**
 * wr_byte - write a byte, one bit at a time
 * @dd: the infinipath device
 * @data: the byte to write
 *
 * Returns 0 if we got the following ack, otherwise 1
 */
static int wr_byte(struct ipath_devdata *dd, u8 data)
{
	int bit_cntr;
	u8 bit;

	for (bit_cntr = 7; bit_cntr >= 0; bit_cntr--) {
		bit = (data >> bit_cntr) & 1;
		sda_out(dd, bit);
		scl_out(dd, i2c_line_high);
		scl_out(dd, i2c_line_low);
	}
	return (!i2c_ackrcv(dd)) ? 1 : 0;
}

static void send_ack(struct ipath_devdata *dd)
{
	sda_out(dd, i2c_line_low);
	scl_out(dd, i2c_line_high);
	scl_out(dd, i2c_line_low);
	sda_out(dd, i2c_line_high);
}

/**
 * i2c_startcmd - transmit the start condition, followed by address/cmd
 * @dd: the infinipath device
 * @offset_dir: direction byte
 *
 *      (both clock/data high, clock high, data low while clock is high)
 */
static int i2c_startcmd(struct ipath_devdata *dd, u8 offset_dir)
{
	int res;

	/* issue start sequence */
	sda_out(dd, i2c_line_high);
	scl_out(dd, i2c_line_high);
	sda_out(dd, i2c_line_low);
	scl_out(dd, i2c_line_low);

	/* issue length and direction byte */
	res = wr_byte(dd, offset_dir);

	if (res)
		ipath_cdbg(VERBOSE, "No ack to complete start\n");

	return res;
}

/**
 * stop_cmd - transmit the stop condition
 * @dd: the infinipath device
 *
 * (both clock/data low, clock high, data high while clock is high)
 */
static void stop_cmd(struct ipath_devdata *dd)
{
	scl_out(dd, i2c_line_low);
	sda_out(dd, i2c_line_low);
	scl_out(dd, i2c_line_high);
	sda_out(dd, i2c_line_high);
	udelay(2);
}

/**
 * eeprom_reset - reset I2C communication
 * @dd: the infinipath device
 */

static int eeprom_reset(struct ipath_devdata *dd)
{
	int clock_cycles_left = 9;
	u64 *gpioval = &dd->ipath_gpio_out;
	int ret;

	eeprom_init = 1;
	*gpioval = ipath_read_kreg64(dd, dd->ipath_kregs->kr_gpio_out);
	ipath_cdbg(VERBOSE, "Resetting i2c eeprom; initial gpioout reg "
		   "is %llx\n", (unsigned long long) *gpioval);

	/*
	 * This is to get the i2c into a known state, by first going low,
	 * then tristate sda (and then tristate scl as first thing
	 * in loop)
	 */
	scl_out(dd, i2c_line_low);
	sda_out(dd, i2c_line_high);

	while (clock_cycles_left--) {
		scl_out(dd, i2c_line_high);

		if (sda_in(dd, 0)) {
			sda_out(dd, i2c_line_low);
			scl_out(dd, i2c_line_low);
			ret = 0;
			goto bail;
		}

		scl_out(dd, i2c_line_low);
	}

	ret = 1;

bail:
	return ret;
}

/**
 * ipath_eeprom_read - receives bytes from the eeprom via I2C
 * @dd: the infinipath device
 * @eeprom_offset: address to read from
 * @buffer: where to store result
 * @len: number of bytes to receive
 */

int ipath_eeprom_read(struct ipath_devdata *dd, u8 eeprom_offset,
		      void *buffer, int len)
{
	/* compiler complains unless initialized */
	u8 single_byte = 0;
	int bit_cntr;
	int ret;

	if (!eeprom_init)
		eeprom_reset(dd);

	eeprom_offset = (eeprom_offset << 1) | READ_CMD;

	if (i2c_startcmd(dd, eeprom_offset)) {
		ipath_dbg("Failed startcmd\n");
		stop_cmd(dd);
		ret = 1;
		goto bail;
	}

	/*
	 * eeprom keeps clocking data out as long as we ack, automatically
	 * incrementing the address.
	 */
	while (len-- > 0) {
		/* get data */
		single_byte = 0;
		for (bit_cntr = 8; bit_cntr; bit_cntr--) {
			u8 bit;
			scl_out(dd, i2c_line_high);
			bit = sda_in(dd, 0);
			single_byte |= bit << (bit_cntr - 1);
			scl_out(dd, i2c_line_low);
		}

		/* send ack if not the last byte */
		if (len)
			send_ack(dd);

		*((u8 *) buffer) = single_byte;
		buffer++;
	}

	stop_cmd(dd);

	ret = 0;

bail:
	return ret;
}

/**
 * ipath_eeprom_write - writes data to the eeprom via I2C
 * @dd: the infinipath device
 * @eeprom_offset: where to place data
 * @buffer: data to write
 * @len: number of bytes to write
 */
int ipath_eeprom_write(struct ipath_devdata *dd, u8 eeprom_offset,
		       const void *buffer, int len)
{
	u8 single_byte;
	int sub_len;
	const u8 *bp = buffer;
	int max_wait_time, i;
	int ret;

	if (!eeprom_init)
		eeprom_reset(dd);

	while (len > 0) {
		if (i2c_startcmd(dd, (eeprom_offset << 1) | WRITE_CMD)) {
			ipath_dbg("Failed to start cmd offset %u\n",
				  eeprom_offset);
			goto failed_write;
		}

		sub_len = min(len, 4);
		eeprom_offset += sub_len;
		len -= sub_len;

		for (i = 0; i < sub_len; i++) {
			if (wr_byte(dd, *bp++)) {
				ipath_dbg("no ack after byte %u/%u (%u "
					  "total remain)\n", i, sub_len,
					  len + sub_len - i);
				goto failed_write;
			}
		}

		stop_cmd(dd);

		/*
		 * wait for write complete by waiting for a successful
		 * read (the chip replies with a zero after the write
		 * cmd completes, and before it writes to the eeprom.
		 * The startcmd for the read will fail the ack until
		 * the writes have completed.   We do this inline to avoid
		 * the debug prints that are in the real read routine
		 * if the startcmd fails.
		 */
		max_wait_time = 100;
		while (i2c_startcmd(dd, READ_CMD)) {
			stop_cmd(dd);
			if (!--max_wait_time) {
				ipath_dbg("Did not get successful read to "
					  "complete write\n");
				goto failed_write;
			}
		}
		/* now read the zero byte */
		for (i = single_byte = 0; i < 8; i++) {
			u8 bit;
			scl_out(dd, i2c_line_high);
			bit = sda_in(dd, 0);
			scl_out(dd, i2c_line_low);
			single_byte <<= 1;
			single_byte |= bit;
		}
		stop_cmd(dd);
	}

	ret = 0;
	goto bail;

failed_write:
	stop_cmd(dd);
	ret = 1;

bail:
	return ret;
}

static u8 flash_csum(struct ipath_flash *ifp, int adjust)
{
	u8 *ip = (u8 *) ifp;
	u8 csum = 0, len;

	for (len = 0; len < ifp->if_length; len++)
		csum += *ip++;
	csum -= ifp->if_csum;
	csum = ~csum;
	if (adjust)
		ifp->if_csum = csum;

	return csum;
}

/**
 * ipath_get_guid - get the GUID from the i2c device
 * @dd: the infinipath device
 *
 * We have the capability to use the ipath_nguid field, and get
 * the guid from the first chip's flash, to use for all of them.
 */
void ipath_get_eeprom_info(struct ipath_devdata *dd)
{
	void *buf;
	struct ipath_flash *ifp;
	__be64 guid;
	int len;
	u8 csum, *bguid;
	int t = dd->ipath_unit;
	struct ipath_devdata *dd0 = ipath_lookup(0);

	if (t && dd0->ipath_nguid > 1 && t <= dd0->ipath_nguid) {
		u8 *bguid, oguid;
		dd->ipath_guid = dd0->ipath_guid;
		bguid = (u8 *) & dd->ipath_guid;

		oguid = bguid[7];
		bguid[7] += t;
		if (oguid > bguid[7]) {
			if (bguid[6] == 0xff) {
				if (bguid[5] == 0xff) {
					ipath_dev_err(
						dd,
						"Can't set %s GUID from "
						"base, wraps to OUI!\n",
						ipath_get_unit_name(t));
					dd->ipath_guid = 0;
					goto bail;
				}
				bguid[5]++;
			}
			bguid[6]++;
		}
		dd->ipath_nguid = 1;

		ipath_dbg("nguid %u, so adding %u to device 0 guid, "
			  "for %llx\n",
			  dd0->ipath_nguid, t,
			  (unsigned long long) be64_to_cpu(dd->ipath_guid));
		goto bail;
	}

	len = offsetof(struct ipath_flash, if_future);
	buf = vmalloc(len);
	if (!buf) {
		ipath_dev_err(dd, "Couldn't allocate memory to read %u "
			      "bytes from eeprom for GUID\n", len);
		goto bail;
	}

	if (ipath_eeprom_read(dd, 0, buf, len)) {
		ipath_dev_err(dd, "Failed reading GUID from eeprom\n");
		goto done;
	}
	ifp = (struct ipath_flash *)buf;

	csum = flash_csum(ifp, 0);
	if (csum != ifp->if_csum) {
		dev_info(&dd->pcidev->dev, "Bad I2C flash checksum: "
			 "0x%x, not 0x%x\n", csum, ifp->if_csum);
		goto done;
	}
	if (*(__be64 *) ifp->if_guid == 0ULL ||
	    *(__be64 *) ifp->if_guid == __constant_cpu_to_be64(-1LL)) {
		ipath_dev_err(dd, "Invalid GUID %llx from flash; "
			      "ignoring\n",
			      *(unsigned long long *) ifp->if_guid);
		/* don't allow GUID if all 0 or all 1's */
		goto done;
	}

	/* complain, but allow it */
	if (*(u64 *) ifp->if_guid == 0x100007511000000ULL)
		dev_info(&dd->pcidev->dev, "Warning, GUID %llx is "
			 "default, probably not correct!\n",
			 *(unsigned long long *) ifp->if_guid);

	bguid = ifp->if_guid;
	if (!bguid[0] && !bguid[1] && !bguid[2]) {
		/* original incorrect GUID format in flash; fix in
		 * core copy, by shifting up 2 octets; don't need to
		 * change top octet, since both it and shifted are
		 * 0.. */
		bguid[1] = bguid[3];
		bguid[2] = bguid[4];
		bguid[3] = bguid[4] = 0;
		guid = *(__be64 *) ifp->if_guid;
		ipath_cdbg(VERBOSE, "Old GUID format in flash, top 3 zero, "
			   "shifting 2 octets\n");
	} else
		guid = *(__be64 *) ifp->if_guid;
	dd->ipath_guid = guid;
	dd->ipath_nguid = ifp->if_numguid;
	/*
	 * Things are slightly complicated by the desire to transparently
	 * support both the Pathscale 10-digit serial number and the QLogic
	 * 13-character version.
	 */
	if ((ifp->if_fversion > 1) && ifp->if_sprefix[0]
		&& ((u8 *)ifp->if_sprefix)[0] != 0xFF) {
		/* This board has a Serial-prefix, which is stored
		 * elsewhere for backward-compatibility.
		 */
		char *snp = dd->ipath_serial;
		int len;
		memcpy(snp, ifp->if_sprefix, sizeof ifp->if_sprefix);
		snp[sizeof ifp->if_sprefix] = '\0';
		len = strlen(snp);
		snp += len;
		len = (sizeof dd->ipath_serial) - len;
		if (len > sizeof ifp->if_serial) {
			len = sizeof ifp->if_serial;
		}
		memcpy(snp, ifp->if_serial, len);
	} else
		memcpy(dd->ipath_serial, ifp->if_serial,
		       sizeof ifp->if_serial);
	if (!strstr(ifp->if_comment, "Tested successfully"))
		ipath_dev_err(dd, "Board SN %s did not pass functional "
			"test: %s\n", dd->ipath_serial,
			ifp->if_comment);

	ipath_cdbg(VERBOSE, "Initted GUID to %llx from eeprom\n",
		   (unsigned long long) be64_to_cpu(dd->ipath_guid));

done:
	vfree(buf);

bail:;
}
