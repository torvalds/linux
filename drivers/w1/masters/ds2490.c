/*
 *	ds2490.c  USB to one wire bridge
 *
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/usb.h>
#include <linux/slab.h>

#include <linux/w1.h>

/* USB Standard */
/* USB Control request vendor type */
#define VENDOR				0x40

/* COMMAND TYPE CODES */
#define CONTROL_CMD			0x00
#define COMM_CMD			0x01
#define MODE_CMD			0x02

/* CONTROL COMMAND CODES */
#define CTL_RESET_DEVICE		0x0000
#define CTL_START_EXE			0x0001
#define CTL_RESUME_EXE			0x0002
#define CTL_HALT_EXE_IDLE		0x0003
#define CTL_HALT_EXE_DONE		0x0004
#define CTL_FLUSH_COMM_CMDS		0x0007
#define CTL_FLUSH_RCV_BUFFER		0x0008
#define CTL_FLUSH_XMT_BUFFER		0x0009
#define CTL_GET_COMM_CMDS		0x000A

/* MODE COMMAND CODES */
#define MOD_PULSE_EN			0x0000
#define MOD_SPEED_CHANGE_EN		0x0001
#define MOD_1WIRE_SPEED			0x0002
#define MOD_STRONG_PU_DURATION		0x0003
#define MOD_PULLDOWN_SLEWRATE		0x0004
#define MOD_PROG_PULSE_DURATION		0x0005
#define MOD_WRITE1_LOWTIME		0x0006
#define MOD_DSOW0_TREC			0x0007

/* COMMUNICATION COMMAND CODES */
#define COMM_ERROR_ESCAPE		0x0601
#define COMM_SET_DURATION		0x0012
#define COMM_BIT_IO			0x0020
#define COMM_PULSE			0x0030
#define COMM_1_WIRE_RESET		0x0042
#define COMM_BYTE_IO			0x0052
#define COMM_MATCH_ACCESS		0x0064
#define COMM_BLOCK_IO			0x0074
#define COMM_READ_STRAIGHT		0x0080
#define COMM_DO_RELEASE			0x6092
#define COMM_SET_PATH			0x00A2
#define COMM_WRITE_SRAM_PAGE		0x00B2
#define COMM_WRITE_EPROM		0x00C4
#define COMM_READ_CRC_PROT_PAGE		0x00D4
#define COMM_READ_REDIRECT_PAGE_CRC	0x21E4
#define COMM_SEARCH_ACCESS		0x00F4

/* Communication command bits */
#define COMM_TYPE			0x0008
#define COMM_SE				0x0008
#define COMM_D				0x0008
#define COMM_Z				0x0008
#define COMM_CH				0x0008
#define COMM_SM				0x0008
#define COMM_R				0x0008
#define COMM_IM				0x0001

#define COMM_PS				0x4000
#define COMM_PST			0x4000
#define COMM_CIB			0x4000
#define COMM_RTS			0x4000
#define COMM_DT				0x2000
#define COMM_SPU			0x1000
#define COMM_F				0x0800
#define COMM_NTF			0x0400
#define COMM_ICP			0x0200
#define COMM_RST			0x0100

#define PULSE_PROG			0x01
#define PULSE_SPUE			0x02

#define BRANCH_MAIN			0xCC
#define BRANCH_AUX			0x33

/* Status flags */
#define ST_SPUA				0x01  /* Strong Pull-up is active */
#define ST_PRGA				0x02  /* 12V programming pulse is being generated */
#define ST_12VP				0x04  /* external 12V programming voltage is present */
#define ST_PMOD				0x08  /* DS2490 powered from USB and external sources */
#define ST_HALT				0x10  /* DS2490 is currently halted */
#define ST_IDLE				0x20  /* DS2490 is currently idle */
#define ST_EPOF				0x80
/* Status transfer size, 16 bytes status, 16 byte result flags */
#define ST_SIZE				0x20

/* Result Register flags */
#define RR_DETECT			0xA5 /* New device detected */
#define RR_NRS				0x01 /* Reset no presence or ... */
#define RR_SH				0x02 /* short on reset or set path */
#define RR_APP				0x04 /* alarming presence on reset */
#define RR_VPP				0x08 /* 12V expected not seen */
#define RR_CMP				0x10 /* compare error */
#define RR_CRC				0x20 /* CRC error detected */
#define RR_RDP				0x40 /* redirected page */
#define RR_EOS				0x80 /* end of search error */

#define SPEED_NORMAL			0x00
#define SPEED_FLEXIBLE			0x01
#define SPEED_OVERDRIVE			0x02

#define NUM_EP				4
#define EP_CONTROL			0
#define EP_STATUS			1
#define EP_DATA_OUT			2
#define EP_DATA_IN			3

struct ds_device
{
	struct list_head	ds_entry;

	struct usb_device	*udev;
	struct usb_interface	*intf;

	int			ep[NUM_EP];

	/* Strong PullUp
	 * 0: pullup not active, else duration in milliseconds
	 */
	int			spu_sleep;
	/* spu_bit contains COMM_SPU or 0 depending on if the strong pullup
	 * should be active or not for writes.
	 */
	u16			spu_bit;

	u8			st_buf[ST_SIZE];
	u8			byte_buf;

	struct w1_bus_master	master;
};

struct ds_status
{
	u8			enable;
	u8			speed;
	u8			pullup_dur;
	u8			ppuls_dur;
	u8			pulldown_slew;
	u8			write1_time;
	u8			write0_time;
	u8			reserved0;
	u8			status;
	u8			command0;
	u8			command1;
	u8			command_buffer_status;
	u8			data_out_buffer_status;
	u8			data_in_buffer_status;
	u8			reserved1;
	u8			reserved2;
};

static LIST_HEAD(ds_devices);
static DEFINE_MUTEX(ds_mutex);

static int ds_send_control_cmd(struct ds_device *dev, u16 value, u16 index)
{
	int err;

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, dev->ep[EP_CONTROL]),
			CONTROL_CMD, VENDOR, value, index, NULL, 0, 1000);
	if (err < 0) {
		pr_err("Failed to send command control message %x.%x: err=%d.\n",
				value, index, err);
		return err;
	}

	return err;
}

static int ds_send_control_mode(struct ds_device *dev, u16 value, u16 index)
{
	int err;

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, dev->ep[EP_CONTROL]),
			MODE_CMD, VENDOR, value, index, NULL, 0, 1000);
	if (err < 0) {
		pr_err("Failed to send mode control message %x.%x: err=%d.\n",
				value, index, err);
		return err;
	}

	return err;
}

static int ds_send_control(struct ds_device *dev, u16 value, u16 index)
{
	int err;

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, dev->ep[EP_CONTROL]),
			COMM_CMD, VENDOR, value, index, NULL, 0, 1000);
	if (err < 0) {
		pr_err("Failed to send control message %x.%x: err=%d.\n",
				value, index, err);
		return err;
	}

	return err;
}

static inline void ds_print_msg(unsigned char *buf, unsigned char *str, int off)
{
	pr_info("%45s: %8x\n", str, buf[off]);
}

static void ds_dump_status(struct ds_device *dev, unsigned char *buf, int count)
{
	int i;

	pr_info("0x%x: count=%d, status: ", dev->ep[EP_STATUS], count);
	for (i=0; i<count; ++i)
		pr_info("%02x ", buf[i]);
	pr_info("\n");

	if (count >= 16) {
		ds_print_msg(buf, "enable flag", 0);
		ds_print_msg(buf, "1-wire speed", 1);
		ds_print_msg(buf, "strong pullup duration", 2);
		ds_print_msg(buf, "programming pulse duration", 3);
		ds_print_msg(buf, "pulldown slew rate control", 4);
		ds_print_msg(buf, "write-1 low time", 5);
		ds_print_msg(buf, "data sample offset/write-0 recovery time",
			6);
		ds_print_msg(buf, "reserved (test register)", 7);
		ds_print_msg(buf, "device status flags", 8);
		ds_print_msg(buf, "communication command byte 1", 9);
		ds_print_msg(buf, "communication command byte 2", 10);
		ds_print_msg(buf, "communication command buffer status", 11);
		ds_print_msg(buf, "1-wire data output buffer status", 12);
		ds_print_msg(buf, "1-wire data input buffer status", 13);
		ds_print_msg(buf, "reserved", 14);
		ds_print_msg(buf, "reserved", 15);
	}
	for (i = 16; i < count; ++i) {
		if (buf[i] == RR_DETECT) {
			ds_print_msg(buf, "new device detect", i);
			continue;
		}
		ds_print_msg(buf, "Result Register Value: ", i);
		if (buf[i] & RR_NRS)
			pr_info("NRS: Reset no presence or ...\n");
		if (buf[i] & RR_SH)
			pr_info("SH: short on reset or set path\n");
		if (buf[i] & RR_APP)
			pr_info("APP: alarming presence on reset\n");
		if (buf[i] & RR_VPP)
			pr_info("VPP: 12V expected not seen\n");
		if (buf[i] & RR_CMP)
			pr_info("CMP: compare error\n");
		if (buf[i] & RR_CRC)
			pr_info("CRC: CRC error detected\n");
		if (buf[i] & RR_RDP)
			pr_info("RDP: redirected page\n");
		if (buf[i] & RR_EOS)
			pr_info("EOS: end of search error\n");
	}
}

static int ds_recv_status(struct ds_device *dev, struct ds_status *st,
			  bool dump)
{
	int count, err;

	if (st)
		memset(st, 0, sizeof(*st));

	count = 0;
	err = usb_interrupt_msg(dev->udev,
				usb_rcvintpipe(dev->udev,
					       dev->ep[EP_STATUS]),
				dev->st_buf, sizeof(dev->st_buf),
				&count, 1000);
	if (err < 0) {
		pr_err("Failed to read 1-wire data from 0x%x: err=%d.\n",
		       dev->ep[EP_STATUS], err);
		return err;
	}

	if (dump)
		ds_dump_status(dev, dev->st_buf, count);

	if (st && count >= sizeof(*st))
		memcpy(st, dev->st_buf, sizeof(*st));

	return count;
}

static void ds_reset_device(struct ds_device *dev)
{
	ds_send_control_cmd(dev, CTL_RESET_DEVICE, 0);
	/* Always allow strong pullup which allow individual writes to use
	 * the strong pullup.
	 */
	if (ds_send_control_mode(dev, MOD_PULSE_EN, PULSE_SPUE))
		pr_err("ds_reset_device: Error allowing strong pullup\n");
	/* Chip strong pullup time was cleared. */
	if (dev->spu_sleep) {
		/* lower 4 bits are 0, see ds_set_pullup */
		u8 del = dev->spu_sleep>>4;
		if (ds_send_control(dev, COMM_SET_DURATION | COMM_IM, del))
			pr_err("ds_reset_device: Error setting duration\n");
	}
}

static int ds_recv_data(struct ds_device *dev, unsigned char *buf, int size)
{
	int count, err;

	/* Careful on size.  If size is less than what is available in
	 * the input buffer, the device fails the bulk transfer and
	 * clears the input buffer.  It could read the maximum size of
	 * the data buffer, but then do you return the first, last, or
	 * some set of the middle size bytes?  As long as the rest of
	 * the code is correct there will be size bytes waiting.  A
	 * call to ds_wait_status will wait until the device is idle
	 * and any data to be received would have been available.
	 */
	count = 0;
	err = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, dev->ep[EP_DATA_IN]),
				buf, size, &count, 1000);
	if (err < 0) {
		pr_info("Clearing ep0x%x.\n", dev->ep[EP_DATA_IN]);
		usb_clear_halt(dev->udev, usb_rcvbulkpipe(dev->udev, dev->ep[EP_DATA_IN]));
		ds_recv_status(dev, NULL, true);
		return err;
	}

#if 0
	{
		int i;

		printk("%s: count=%d: ", __func__, count);
		for (i=0; i<count; ++i)
			printk("%02x ", buf[i]);
		printk("\n");
	}
#endif
	return count;
}

static int ds_send_data(struct ds_device *dev, unsigned char *buf, int len)
{
	int count, err;

	count = 0;
	err = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, dev->ep[EP_DATA_OUT]), buf, len, &count, 1000);
	if (err < 0) {
		pr_err("Failed to write 1-wire data to ep0x%x: "
			"err=%d.\n", dev->ep[EP_DATA_OUT], err);
		return err;
	}

	return err;
}

#if 0

int ds_stop_pulse(struct ds_device *dev, int limit)
{
	struct ds_status st;
	int count = 0, err = 0;

	do {
		err = ds_send_control(dev, CTL_HALT_EXE_IDLE, 0);
		if (err)
			break;
		err = ds_send_control(dev, CTL_RESUME_EXE, 0);
		if (err)
			break;
		err = ds_recv_status(dev, &st, false);
		if (err)
			break;

		if ((st.status & ST_SPUA) == 0) {
			err = ds_send_control_mode(dev, MOD_PULSE_EN, 0);
			if (err)
				break;
		}
	} while(++count < limit);

	return err;
}

int ds_detect(struct ds_device *dev, struct ds_status *st)
{
	int err;

	err = ds_send_control_cmd(dev, CTL_RESET_DEVICE, 0);
	if (err)
		return err;

	err = ds_send_control(dev, COMM_SET_DURATION | COMM_IM, 0);
	if (err)
		return err;

	err = ds_send_control(dev, COMM_SET_DURATION | COMM_IM | COMM_TYPE, 0x40);
	if (err)
		return err;

	err = ds_send_control_mode(dev, MOD_PULSE_EN, PULSE_PROG);
	if (err)
		return err;

	err = ds_dump_status(dev, st);

	return err;
}

#endif  /*  0  */

static int ds_wait_status(struct ds_device *dev, struct ds_status *st)
{
	int err, count = 0;

	do {
		st->status = 0;
		err = ds_recv_status(dev, st, false);
#if 0
		if (err >= 0) {
			int i;
			printk("0x%x: count=%d, status: ", dev->ep[EP_STATUS], err);
			for (i=0; i<err; ++i)
				printk("%02x ", dev->st_buf[i]);
			printk("\n");
		}
#endif
	} while (!(st->status & ST_IDLE) && !(err < 0) && ++count < 100);

	if (err >= 16 && st->status & ST_EPOF) {
		pr_info("Resetting device after ST_EPOF.\n");
		ds_reset_device(dev);
		/* Always dump the device status. */
		count = 101;
	}

	/* Dump the status for errors or if there is extended return data.
	 * The extended status includes new device detection (maybe someone
	 * can do something with it).
	 */
	if (err > 16 || count >= 100 || err < 0)
		ds_dump_status(dev, dev->st_buf, err);

	/* Extended data isn't an error.  Well, a short is, but the dump
	 * would have already told the user that and we can't do anything
	 * about it in software anyway.
	 */
	if (count >= 100 || err < 0)
		return -1;
	else
		return 0;
}

static int ds_reset(struct ds_device *dev)
{
	int err;

	/* Other potentionally interesting flags for reset.
	 *
	 * COMM_NTF: Return result register feedback.  This could be used to
	 * detect some conditions such as short, alarming presence, or
	 * detect if a new device was detected.
	 *
	 * COMM_SE which allows SPEED_NORMAL, SPEED_FLEXIBLE, SPEED_OVERDRIVE:
	 * Select the data transfer rate.
	 */
	err = ds_send_control(dev, COMM_1_WIRE_RESET | COMM_IM, SPEED_NORMAL);
	if (err)
		return err;

	return 0;
}

#if 0
static int ds_set_speed(struct ds_device *dev, int speed)
{
	int err;

	if (speed != SPEED_NORMAL && speed != SPEED_FLEXIBLE && speed != SPEED_OVERDRIVE)
		return -EINVAL;

	if (speed != SPEED_OVERDRIVE)
		speed = SPEED_FLEXIBLE;

	speed &= 0xff;

	err = ds_send_control_mode(dev, MOD_1WIRE_SPEED, speed);
	if (err)
		return err;

	return err;
}
#endif  /*  0  */

static int ds_set_pullup(struct ds_device *dev, int delay)
{
	int err = 0;
	u8 del = 1 + (u8)(delay >> 4);
	/* Just storing delay would not get the trunication and roundup. */
	int ms = del<<4;

	/* Enable spu_bit if a delay is set. */
	dev->spu_bit = delay ? COMM_SPU : 0;
	/* If delay is zero, it has already been disabled, if the time is
	 * the same as the hardware was last programmed to, there is also
	 * nothing more to do.  Compare with the recalculated value ms
	 * rather than del or delay which can have a different value.
	 */
	if (delay == 0 || ms == dev->spu_sleep)
		return err;

	err = ds_send_control(dev, COMM_SET_DURATION | COMM_IM, del);
	if (err)
		return err;

	dev->spu_sleep = ms;

	return err;
}

static int ds_touch_bit(struct ds_device *dev, u8 bit, u8 *tbit)
{
	int err;
	struct ds_status st;

	err = ds_send_control(dev, COMM_BIT_IO | COMM_IM | (bit ? COMM_D : 0),
		0);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_recv_data(dev, tbit, sizeof(*tbit));
	if (err < 0)
		return err;

	return 0;
}

#if 0
static int ds_write_bit(struct ds_device *dev, u8 bit)
{
	int err;
	struct ds_status st;

	/* Set COMM_ICP to write without a readback.  Note, this will
	 * produce one time slot, a down followed by an up with COMM_D
	 * only determing the timing.
	 */
	err = ds_send_control(dev, COMM_BIT_IO | COMM_IM | COMM_ICP |
		(bit ? COMM_D : 0), 0);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	return 0;
}
#endif

static int ds_write_byte(struct ds_device *dev, u8 byte)
{
	int err;
	struct ds_status st;

	err = ds_send_control(dev, COMM_BYTE_IO | COMM_IM | dev->spu_bit, byte);
	if (err)
		return err;

	if (dev->spu_bit)
		msleep(dev->spu_sleep);

	err = ds_wait_status(dev, &st);
	if (err)
		return err;

	err = ds_recv_data(dev, &dev->byte_buf, 1);
	if (err < 0)
		return err;

	return !(byte == dev->byte_buf);
}

static int ds_read_byte(struct ds_device *dev, u8 *byte)
{
	int err;
	struct ds_status st;

	err = ds_send_control(dev, COMM_BYTE_IO | COMM_IM , 0xff);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_recv_data(dev, byte, sizeof(*byte));
	if (err < 0)
		return err;

	return 0;
}

static int ds_read_block(struct ds_device *dev, u8 *buf, int len)
{
	struct ds_status st;
	int err;

	if (len > 64*1024)
		return -E2BIG;

	memset(buf, 0xFF, len);

	err = ds_send_data(dev, buf, len);
	if (err < 0)
		return err;

	err = ds_send_control(dev, COMM_BLOCK_IO | COMM_IM, len);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	memset(buf, 0x00, len);
	err = ds_recv_data(dev, buf, len);

	return err;
}

static int ds_write_block(struct ds_device *dev, u8 *buf, int len)
{
	int err;
	struct ds_status st;

	err = ds_send_data(dev, buf, len);
	if (err < 0)
		return err;

	err = ds_send_control(dev, COMM_BLOCK_IO | COMM_IM | dev->spu_bit, len);
	if (err)
		return err;

	if (dev->spu_bit)
		msleep(dev->spu_sleep);

	ds_wait_status(dev, &st);

	err = ds_recv_data(dev, buf, len);
	if (err < 0)
		return err;

	return !(err == len);
}

static void ds9490r_search(void *data, struct w1_master *master,
	u8 search_type, w1_slave_found_callback callback)
{
	/* When starting with an existing id, the first id returned will
	 * be that device (if it is still on the bus most likely).
	 *
	 * If the number of devices found is less than or equal to the
	 * search_limit, that number of IDs will be returned.  If there are
	 * more, search_limit IDs will be returned followed by a non-zero
	 * discrepency value.
	 */
	struct ds_device *dev = data;
	int err;
	u16 value, index;
	struct ds_status st;
	int search_limit;
	int found = 0;
	int i;

	/* DS18b20 spec, 13.16 ms per device, 75 per second, sleep for
	 * discovering 8 devices (1 bulk transfer and 1/2 FIFO size) at a time.
	 */
	const unsigned long jtime = msecs_to_jiffies(1000*8/75);
	/* FIFO 128 bytes, bulk packet size 64, read a multiple of the
	 * packet size.
	 */
	const size_t bufsize = 2 * 64;
	u64 *buf;

	buf = kmalloc(bufsize, GFP_KERNEL);
	if (!buf)
		return;

	mutex_lock(&master->bus_mutex);

	/* address to start searching at */
	if (ds_send_data(dev, (u8 *)&master->search_id, 8) < 0)
		goto search_out;
	master->search_id = 0;

	value = COMM_SEARCH_ACCESS | COMM_IM | COMM_RST | COMM_SM | COMM_F |
		COMM_RTS;
	search_limit = master->max_slave_count;
	if (search_limit > 255)
		search_limit = 0;
	index = search_type | (search_limit << 8);
	if (ds_send_control(dev, value, index) < 0)
		goto search_out;

	do {
		schedule_timeout(jtime);

		err = ds_recv_status(dev, &st, false);
		if (err < 0 || err < sizeof(st))
			break;

		if (st.data_in_buffer_status) {
			/* Bulk in can receive partial ids, but when it does
			 * they fail crc and will be discarded anyway.
			 * That has only been seen when status in buffer
			 * is 0 and bulk is read anyway, so don't read
			 * bulk without first checking if status says there
			 * is data to read.
			 */
			err = ds_recv_data(dev, (u8 *)buf, bufsize);
			if (err < 0)
				break;
			for (i = 0; i < err/8; ++i) {
				++found;
				if (found <= search_limit)
					callback(master, buf[i]);
				/* can't know if there will be a discrepancy
				 * value after until the next id */
				if (found == search_limit)
					master->search_id = buf[i];
			}
		}

		if (test_bit(W1_ABORT_SEARCH, &master->flags))
			break;
	} while (!(st.status & (ST_IDLE | ST_HALT)));

	/* only continue the search if some weren't found */
	if (found <= search_limit) {
		master->search_id = 0;
	} else if (!test_bit(W1_WARN_MAX_COUNT, &master->flags)) {
		/* Only max_slave_count will be scanned in a search,
		 * but it will start where it left off next search
		 * until all ids are identified and then it will start
		 * over.  A continued search will report the previous
		 * last id as the first id (provided it is still on the
		 * bus).
		 */
		dev_info(&dev->udev->dev, "%s: max_slave_count %d reached, "
			"will continue next search.\n", __func__,
			master->max_slave_count);
		set_bit(W1_WARN_MAX_COUNT, &master->flags);
	}
search_out:
	mutex_unlock(&master->bus_mutex);
	kfree(buf);
}

#if 0
/*
 * FIXME: if this disabled code is ever used in the future all ds_send_data()
 * calls must be changed to use a DMAable buffer.
 */
static int ds_match_access(struct ds_device *dev, u64 init)
{
	int err;
	struct ds_status st;

	err = ds_send_data(dev, (unsigned char *)&init, sizeof(init));
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_send_control(dev, COMM_MATCH_ACCESS | COMM_IM | COMM_RST, 0x0055);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	return 0;
}

static int ds_set_path(struct ds_device *dev, u64 init)
{
	int err;
	struct ds_status st;
	u8 buf[9];

	memcpy(buf, &init, 8);
	buf[8] = BRANCH_MAIN;

	err = ds_send_data(dev, buf, sizeof(buf));
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_send_control(dev, COMM_SET_PATH | COMM_IM | COMM_RST, 0);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	return 0;
}

#endif  /*  0  */

static u8 ds9490r_touch_bit(void *data, u8 bit)
{
	struct ds_device *dev = data;

	if (ds_touch_bit(dev, bit, &dev->byte_buf))
		return 0;

	return dev->byte_buf;
}

#if 0
static void ds9490r_write_bit(void *data, u8 bit)
{
	struct ds_device *dev = data;

	ds_write_bit(dev, bit);
}

static u8 ds9490r_read_bit(void *data)
{
	struct ds_device *dev = data;
	int err;

	err = ds_touch_bit(dev, 1, &dev->byte_buf);
	if (err)
		return 0;

	return dev->byte_buf & 1;
}
#endif

static void ds9490r_write_byte(void *data, u8 byte)
{
	struct ds_device *dev = data;

	ds_write_byte(dev, byte);
}

static u8 ds9490r_read_byte(void *data)
{
	struct ds_device *dev = data;
	int err;

	err = ds_read_byte(dev, &dev->byte_buf);
	if (err)
		return 0;

	return dev->byte_buf;
}

static void ds9490r_write_block(void *data, const u8 *buf, int len)
{
	struct ds_device *dev = data;
	u8 *tbuf;

	if (len <= 0)
		return;

	tbuf = kmemdup(buf, len, GFP_KERNEL);
	if (!tbuf)
		return;

	ds_write_block(dev, tbuf, len);

	kfree(tbuf);
}

static u8 ds9490r_read_block(void *data, u8 *buf, int len)
{
	struct ds_device *dev = data;
	int err;
	u8 *tbuf;

	if (len <= 0)
		return 0;

	tbuf = kmalloc(len, GFP_KERNEL);
	if (!tbuf)
		return 0;

	err = ds_read_block(dev, tbuf, len);
	if (err >= 0)
		memcpy(buf, tbuf, len);

	kfree(tbuf);

	return err >= 0 ? len : 0;
}

static u8 ds9490r_reset(void *data)
{
	struct ds_device *dev = data;
	int err;

	err = ds_reset(dev);
	if (err)
		return 1;

	return 0;
}

static u8 ds9490r_set_pullup(void *data, int delay)
{
	struct ds_device *dev = data;

	if (ds_set_pullup(dev, delay))
		return 1;

	return 0;
}

static int ds_w1_init(struct ds_device *dev)
{
	memset(&dev->master, 0, sizeof(struct w1_bus_master));

	/* Reset the device as it can be in a bad state.
	 * This is necessary because a block write will wait for data
	 * to be placed in the output buffer and block any later
	 * commands which will keep accumulating and the device will
	 * not be idle.  Another case is removing the ds2490 module
	 * while a bus search is in progress, somehow a few commands
	 * get through, but the input transfers fail leaving data in
	 * the input buffer.  This will cause the next read to fail
	 * see the note in ds_recv_data.
	 */
	ds_reset_device(dev);

	dev->master.data	= dev;
	dev->master.touch_bit	= &ds9490r_touch_bit;
	/* read_bit and write_bit in w1_bus_master are expected to set and
	 * sample the line level.  For write_bit that means it is expected to
	 * set it to that value and leave it there.  ds2490 only supports an
	 * individual time slot at the lowest level.  The requirement from
	 * pulling the bus state down to reading the state is 15us, something
	 * that isn't realistic on the USB bus anyway.
	dev->master.read_bit	= &ds9490r_read_bit;
	dev->master.write_bit	= &ds9490r_write_bit;
	*/
	dev->master.read_byte	= &ds9490r_read_byte;
	dev->master.write_byte	= &ds9490r_write_byte;
	dev->master.read_block	= &ds9490r_read_block;
	dev->master.write_block	= &ds9490r_write_block;
	dev->master.reset_bus	= &ds9490r_reset;
	dev->master.set_pullup	= &ds9490r_set_pullup;
	dev->master.search	= &ds9490r_search;

	return w1_add_master_device(&dev->master);
}

static void ds_w1_fini(struct ds_device *dev)
{
	w1_remove_master_device(&dev->master);
}

static int ds_probe(struct usb_interface *intf,
		    const struct usb_device_id *udev_id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface *iface_desc;
	struct ds_device *dev;
	int i, err, alt;

	dev = kzalloc(sizeof(struct ds_device), GFP_KERNEL);
	if (!dev) {
		pr_info("Failed to allocate new DS9490R structure.\n");
		return -ENOMEM;
	}
	dev->udev = usb_get_dev(udev);
	if (!dev->udev) {
		err = -ENOMEM;
		goto err_out_free;
	}
	memset(dev->ep, 0, sizeof(dev->ep));

	usb_set_intfdata(intf, dev);

	err = usb_reset_configuration(dev->udev);
	if (err) {
		dev_err(&dev->udev->dev,
			"Failed to reset configuration: err=%d.\n", err);
		goto err_out_clear;
	}

	/* alternative 3, 1ms interrupt (greatly speeds search), 64 byte bulk */
	alt = 3;
	err = usb_set_interface(dev->udev,
		intf->altsetting[alt].desc.bInterfaceNumber, alt);
	if (err) {
		dev_err(&dev->udev->dev, "Failed to set alternative setting %d "
			"for %d interface: err=%d.\n", alt,
			intf->altsetting[alt].desc.bInterfaceNumber, err);
		goto err_out_clear;
	}

	iface_desc = &intf->altsetting[alt];
	if (iface_desc->desc.bNumEndpoints != NUM_EP-1) {
		pr_info("Num endpoints=%d. It is not DS9490R.\n",
			iface_desc->desc.bNumEndpoints);
		err = -EINVAL;
		goto err_out_clear;
	}

	/*
	 * This loop doesn'd show control 0 endpoint,
	 * so we will fill only 1-3 endpoints entry.
	 */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		dev->ep[i+1] = endpoint->bEndpointAddress;
#if 0
		printk("%d: addr=%x, size=%d, dir=%s, type=%x\n",
			i, endpoint->bEndpointAddress, le16_to_cpu(endpoint->wMaxPacketSize),
			(endpoint->bEndpointAddress & USB_DIR_IN)?"IN":"OUT",
			endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
#endif
	}

	err = ds_w1_init(dev);
	if (err)
		goto err_out_clear;

	mutex_lock(&ds_mutex);
	list_add_tail(&dev->ds_entry, &ds_devices);
	mutex_unlock(&ds_mutex);

	return 0;

err_out_clear:
	usb_set_intfdata(intf, NULL);
	usb_put_dev(dev->udev);
err_out_free:
	kfree(dev);
	return err;
}

static void ds_disconnect(struct usb_interface *intf)
{
	struct ds_device *dev;

	dev = usb_get_intfdata(intf);
	if (!dev)
		return;

	mutex_lock(&ds_mutex);
	list_del(&dev->ds_entry);
	mutex_unlock(&ds_mutex);

	ds_w1_fini(dev);

	usb_set_intfdata(intf, NULL);

	usb_put_dev(dev->udev);
	kfree(dev);
}

static const struct usb_device_id ds_id_table[] = {
	{ USB_DEVICE(0x04fa, 0x2490) },
	{ },
};
MODULE_DEVICE_TABLE(usb, ds_id_table);

static struct usb_driver ds_driver = {
	.name =		"DS9490R",
	.probe =	ds_probe,
	.disconnect =	ds_disconnect,
	.id_table =	ds_id_table,
};
module_usb_driver(ds_driver);

MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("DS2490 USB <-> W1 bus master driver (DS9490*)");
MODULE_LICENSE("GPL");
