/*
 *  Shared Transport Line discipline driver Core
 *	Init Manager module responsible for GPIO control
 *	and firmware download
 *  Copyright (C) 2009-2010 Texas Instruments
 *  Author: Pavan Savoy <pavan_savoy@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define pr_fmt(fmt) "(stk) :" fmt
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/gpio.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/tty.h>

#include <linux/skbuff.h>
#include <linux/ti_wilink_st.h>
#include <linux/module.h>


#define MAX_ST_DEVICES	3	/* Imagine 1 on each UART for now */
static struct platform_device *st_kim_devices[MAX_ST_DEVICES];

/**********************************************************************/
/* internal functions */

/**
 * st_get_plat_device -
 *	function which returns the reference to the platform device
 *	requested by id. As of now only 1 such device exists (id=0)
 *	the context requesting for reference can get the id to be
 *	requested by a. The protocol driver which is registering or
 *	b. the tty device which is opened.
 */
static struct platform_device *st_get_plat_device(int id)
{
	return st_kim_devices[id];
}

/**
 * validate_firmware_response -
 *	function to return whether the firmware response was proper
 *	in case of error don't complete so that waiting for proper
 *	response times out
 */
static void validate_firmware_response(struct kim_data_s *kim_gdata)
{
	struct sk_buff *skb = kim_gdata->rx_skb;
	if (!skb)
		return;

	/* these magic numbers are the position in the response buffer which
	 * allows us to distinguish whether the response is for the read
	 * version info. command
	 */
	if (skb->data[2] == 0x01 && skb->data[3] == 0x01 &&
			skb->data[4] == 0x10 && skb->data[5] == 0x00) {
		/* fw version response */
		memcpy(kim_gdata->resp_buffer,
				kim_gdata->rx_skb->data,
				kim_gdata->rx_skb->len);
		complete_all(&kim_gdata->kim_rcvd);
		kim_gdata->rx_state = ST_W4_PACKET_TYPE;
		kim_gdata->rx_skb = NULL;
		kim_gdata->rx_count = 0;
	} else if (unlikely(skb->data[5] != 0)) {
		pr_err("no proper response during fw download");
		pr_err("data6 %x", skb->data[5]);
		kfree_skb(skb);
		return;		/* keep waiting for the proper response */
	}
	/* becos of all the script being downloaded */
	complete_all(&kim_gdata->kim_rcvd);
	kfree_skb(skb);
}

/* check for data len received inside kim_int_recv
 * most often hit the last case to update state to waiting for data
 */
static inline int kim_check_data_len(struct kim_data_s *kim_gdata, int len)
{
	register int room = skb_tailroom(kim_gdata->rx_skb);

	pr_debug("len %d room %d", len, room);

	if (!len) {
		validate_firmware_response(kim_gdata);
	} else if (len > room) {
		/* Received packet's payload length is larger.
		 * We can't accommodate it in created skb.
		 */
		pr_err("Data length is too large len %d room %d", len,
			   room);
		kfree_skb(kim_gdata->rx_skb);
	} else {
		/* Packet header has non-zero payload length and
		 * we have enough space in created skb. Lets read
		 * payload data */
		kim_gdata->rx_state = ST_W4_DATA;
		kim_gdata->rx_count = len;
		return len;
	}

	/* Change ST LL state to continue to process next
	 * packet */
	kim_gdata->rx_state = ST_W4_PACKET_TYPE;
	kim_gdata->rx_skb = NULL;
	kim_gdata->rx_count = 0;

	return 0;
}

/**
 * kim_int_recv - receive function called during firmware download
 *	firmware download responses on different UART drivers
 *	have been observed to come in bursts of different
 *	tty_receive and hence the logic
 */
static void kim_int_recv(struct kim_data_s *kim_gdata,
	const unsigned char *data, long count)
{
	const unsigned char *ptr;
	int len = 0, type = 0;
	unsigned char *plen;

	pr_debug("%s", __func__);
	/* Decode received bytes here */
	ptr = data;
	if (unlikely(ptr == NULL)) {
		pr_err(" received null from TTY ");
		return;
	}

	while (count) {
		if (kim_gdata->rx_count) {
			len = min_t(unsigned int, kim_gdata->rx_count, count);
			memcpy(skb_put(kim_gdata->rx_skb, len), ptr, len);
			kim_gdata->rx_count -= len;
			count -= len;
			ptr += len;

			if (kim_gdata->rx_count)
				continue;

			/* Check ST RX state machine , where are we? */
			switch (kim_gdata->rx_state) {
				/* Waiting for complete packet ? */
			case ST_W4_DATA:
				pr_debug("Complete pkt received");
				validate_firmware_response(kim_gdata);
				kim_gdata->rx_state = ST_W4_PACKET_TYPE;
				kim_gdata->rx_skb = NULL;
				continue;
				/* Waiting for Bluetooth event header ? */
			case ST_W4_HEADER:
				plen =
				(unsigned char *)&kim_gdata->rx_skb->data[1];
				pr_debug("event hdr: plen 0x%02x\n", *plen);
				kim_check_data_len(kim_gdata, *plen);
				continue;
			}	/* end of switch */
		}		/* end of if rx_state */
		switch (*ptr) {
			/* Bluetooth event packet? */
		case 0x04:
			kim_gdata->rx_state = ST_W4_HEADER;
			kim_gdata->rx_count = 2;
			type = *ptr;
			break;
		default:
			pr_info("unknown packet");
			ptr++;
			count--;
			continue;
		}
		ptr++;
		count--;
		kim_gdata->rx_skb =
			alloc_skb(1024+8, GFP_ATOMIC);
		if (!kim_gdata->rx_skb) {
			pr_err("can't allocate mem for new packet");
			kim_gdata->rx_state = ST_W4_PACKET_TYPE;
			kim_gdata->rx_count = 0;
			return;
		}
		skb_reserve(kim_gdata->rx_skb, 8);
		kim_gdata->rx_skb->cb[0] = 4;
		kim_gdata->rx_skb->cb[1] = 0;

	}
	return;
}

static long read_local_version(struct kim_data_s *kim_gdata, char *bts_scr_name)
{
	unsigned short version = 0, chip = 0, min_ver = 0, maj_ver = 0;
	const char read_ver_cmd[] = { 0x01, 0x01, 0x10, 0x00 };

	pr_debug("%s", __func__);

	reinit_completion(&kim_gdata->kim_rcvd);
	if (4 != st_int_write(kim_gdata->core_data, read_ver_cmd, 4)) {
		pr_err("kim: couldn't write 4 bytes");
		return -EIO;
	}

	if (!wait_for_completion_interruptible_timeout(
		&kim_gdata->kim_rcvd, msecs_to_jiffies(CMD_RESP_TIME))) {
		pr_err(" waiting for ver info- timed out ");
		return -ETIMEDOUT;
	}
	reinit_completion(&kim_gdata->kim_rcvd);
	/* the positions 12 & 13 in the response buffer provide with the
	 * chip, major & minor numbers
	 */

	version =
		MAKEWORD(kim_gdata->resp_buffer[12],
				kim_gdata->resp_buffer[13]);
	chip = (version & 0x7C00) >> 10;
	min_ver = (version & 0x007F);
	maj_ver = (version & 0x0380) >> 7;

	if (version & 0x8000)
		maj_ver |= 0x0008;

	sprintf(bts_scr_name, "ti-connectivity/TIInit_%d.%d.%d.bts",
		chip, maj_ver, min_ver);

	/* to be accessed later via sysfs entry */
	kim_gdata->version.full = version;
	kim_gdata->version.chip = chip;
	kim_gdata->version.maj_ver = maj_ver;
	kim_gdata->version.min_ver = min_ver;

	pr_info("%s", bts_scr_name);
	return 0;
}

static void skip_change_remote_baud(unsigned char **ptr, long *len)
{
	unsigned char *nxt_action, *cur_action;
	cur_action = *ptr;

	nxt_action = cur_action + sizeof(struct bts_action) +
		((struct bts_action *) cur_action)->size;

	if (((struct bts_action *) nxt_action)->type != ACTION_WAIT_EVENT) {
		pr_err("invalid action after change remote baud command");
	} else {
		*ptr = *ptr + sizeof(struct bts_action) +
			((struct bts_action *)cur_action)->size;
		*len = *len - (sizeof(struct bts_action) +
				((struct bts_action *)cur_action)->size);
		/* warn user on not commenting these in firmware */
		pr_warn("skipping the wait event of change remote baud");
	}
}

/**
 * download_firmware -
 *	internal function which parses through the .bts firmware
 *	script file intreprets SEND, DELAY actions only as of now
 */
static long download_firmware(struct kim_data_s *kim_gdata)
{
	long err = 0;
	long len = 0;
	unsigned char *ptr = NULL;
	unsigned char *action_ptr = NULL;
	unsigned char bts_scr_name[40] = { 0 };	/* 40 char long bts scr name? */
	int wr_room_space;
	int cmd_size;
	unsigned long timeout;

	err = read_local_version(kim_gdata, bts_scr_name);
	if (err != 0) {
		pr_err("kim: failed to read local ver");
		return err;
	}
	err =
	    request_firmware(&kim_gdata->fw_entry, bts_scr_name,
			     &kim_gdata->kim_pdev->dev);
	if (unlikely((err != 0) || (kim_gdata->fw_entry->data == NULL) ||
		     (kim_gdata->fw_entry->size == 0))) {
		pr_err(" request_firmware failed(errno %ld) for %s", err,
			   bts_scr_name);
		return -EINVAL;
	}
	ptr = (void *)kim_gdata->fw_entry->data;
	len = kim_gdata->fw_entry->size;
	/* bts_header to remove out magic number and
	 * version
	 */
	ptr += sizeof(struct bts_header);
	len -= sizeof(struct bts_header);

	while (len > 0 && ptr) {
		pr_debug(" action size %d, type %d ",
			   ((struct bts_action *)ptr)->size,
			   ((struct bts_action *)ptr)->type);

		switch (((struct bts_action *)ptr)->type) {
		case ACTION_SEND_COMMAND:	/* action send */
			pr_debug("S");
			action_ptr = &(((struct bts_action *)ptr)->data[0]);
			if (unlikely
			    (((struct hci_command *)action_ptr)->opcode ==
			     0xFF36)) {
				/* ignore remote change
				 * baud rate HCI VS command */
				pr_warn("change remote baud"
				    " rate command in firmware");
				skip_change_remote_baud(&ptr, &len);
				break;
			}
			/*
			 * Make sure we have enough free space in uart
			 * tx buffer to write current firmware command
			 */
			cmd_size = ((struct bts_action *)ptr)->size;
			timeout = jiffies + msecs_to_jiffies(CMD_WR_TIME);
			do {
				wr_room_space =
					st_get_uart_wr_room(kim_gdata->core_data);
				if (wr_room_space < 0) {
					pr_err("Unable to get free "
							"space info from uart tx buffer");
					release_firmware(kim_gdata->fw_entry);
					return wr_room_space;
				}
				mdelay(1); /* wait 1ms before checking room */
			} while ((wr_room_space < cmd_size) &&
					time_before(jiffies, timeout));

			/* Timeout happened ? */
			if (time_after_eq(jiffies, timeout)) {
				pr_err("Timeout while waiting for free "
						"free space in uart tx buffer");
				release_firmware(kim_gdata->fw_entry);
				return -ETIMEDOUT;
			}
			/* reinit completion before sending for the
			 * relevant wait
			 */
			reinit_completion(&kim_gdata->kim_rcvd);

			/*
			 * Free space found in uart buffer, call st_int_write
			 * to send current firmware command to the uart tx
			 * buffer.
			 */
			err = st_int_write(kim_gdata->core_data,
			((struct bts_action_send *)action_ptr)->data,
					   ((struct bts_action *)ptr)->size);
			if (unlikely(err < 0)) {
				release_firmware(kim_gdata->fw_entry);
				return err;
			}
			/*
			 * Check number of bytes written to the uart tx buffer
			 * and requested command write size
			 */
			if (err != cmd_size) {
				pr_err("Number of bytes written to uart "
						"tx buffer are not matching with "
						"requested cmd write size");
				release_firmware(kim_gdata->fw_entry);
				return -EIO;
			}
			break;
		case ACTION_WAIT_EVENT:  /* wait */
			pr_debug("W");
			if (!wait_for_completion_interruptible_timeout(
					&kim_gdata->kim_rcvd,
					msecs_to_jiffies(CMD_RESP_TIME))) {
				pr_err("response timeout during fw download ");
				/* timed out */
				release_firmware(kim_gdata->fw_entry);
				return -ETIMEDOUT;
			}
			reinit_completion(&kim_gdata->kim_rcvd);
			break;
		case ACTION_DELAY:	/* sleep */
			pr_info("sleep command in scr");
			action_ptr = &(((struct bts_action *)ptr)->data[0]);
			mdelay(((struct bts_action_delay *)action_ptr)->msec);
			break;
		}
		len =
		    len - (sizeof(struct bts_action) +
			   ((struct bts_action *)ptr)->size);
		ptr =
		    ptr + sizeof(struct bts_action) +
		    ((struct bts_action *)ptr)->size;
	}
	/* fw download complete */
	release_firmware(kim_gdata->fw_entry);
	return 0;
}

/**********************************************************************/
/* functions called from ST core */
/* called from ST Core, when REG_IN_PROGRESS (registration in progress)
 * can be because of
 * 1. response to read local version
 * 2. during send/recv's of firmware download
 */
void st_kim_recv(void *disc_data, const unsigned char *data, long count)
{
	struct st_data_s	*st_gdata = (struct st_data_s *)disc_data;
	struct kim_data_s	*kim_gdata = st_gdata->kim_data;

	/* proceed to gather all data and distinguish read fw version response
	 * from other fw responses when data gathering is complete
	 */
	kim_int_recv(kim_gdata, data, count);
	return;
}

/* to signal completion of line discipline installation
 * called from ST Core, upon tty_open
 */
void st_kim_complete(void *kim_data)
{
	struct kim_data_s	*kim_gdata = (struct kim_data_s *)kim_data;
	complete(&kim_gdata->ldisc_installed);
}

/**
 * st_kim_start - called from ST Core upon 1st registration
 *	This involves toggling the chip enable gpio, reading
 *	the firmware version from chip, forming the fw file name
 *	based on the chip version, requesting the fw, parsing it
 *	and perform download(send/recv).
 */
long st_kim_start(void *kim_data)
{
	long err = 0;
	long retry = POR_RETRY_COUNT;
	struct ti_st_plat_data	*pdata;
	struct kim_data_s	*kim_gdata = (struct kim_data_s *)kim_data;

	pr_info(" %s", __func__);
	pdata = kim_gdata->kim_pdev->dev.platform_data;

	do {
		/* platform specific enabling code here */
		if (pdata->chip_enable)
			pdata->chip_enable(kim_gdata);

		/* Configure BT nShutdown to HIGH state */
		gpio_set_value(kim_gdata->nshutdown, GPIO_LOW);
		mdelay(5);	/* FIXME: a proper toggle */
		gpio_set_value(kim_gdata->nshutdown, GPIO_HIGH);
		mdelay(100);
		/* re-initialize the completion */
		reinit_completion(&kim_gdata->ldisc_installed);
		/* send notification to UIM */
		kim_gdata->ldisc_install = 1;
		pr_info("ldisc_install = 1");
		sysfs_notify(&kim_gdata->kim_pdev->dev.kobj,
				NULL, "install");
		/* wait for ldisc to be installed */
		err = wait_for_completion_interruptible_timeout(
			&kim_gdata->ldisc_installed, msecs_to_jiffies(LDISC_TIME));
		if (!err) {
			/* ldisc installation timeout,
			 * flush uart, power cycle BT_EN */
			pr_err("ldisc installation timeout");
			err = st_kim_stop(kim_gdata);
			continue;
		} else {
			/* ldisc installed now */
			pr_info("line discipline installed");
			err = download_firmware(kim_gdata);
			if (err != 0) {
				/* ldisc installed but fw download failed,
				 * flush uart & power cycle BT_EN */
				pr_err("download firmware failed");
				err = st_kim_stop(kim_gdata);
				continue;
			} else {	/* on success don't retry */
				break;
			}
		}
	} while (retry--);
	return err;
}

/**
 * st_kim_stop - stop communication with chip.
 *	This can be called from ST Core/KIM, on the-
 *	(a) last un-register when chip need not be powered there-after,
 *	(b) upon failure to either install ldisc or download firmware.
 *	The function is responsible to (a) notify UIM about un-installation,
 *	(b) flush UART if the ldisc was installed.
 *	(c) reset BT_EN - pull down nshutdown at the end.
 *	(d) invoke platform's chip disabling routine.
 */
long st_kim_stop(void *kim_data)
{
	long err = 0;
	struct kim_data_s	*kim_gdata = (struct kim_data_s *)kim_data;
	struct ti_st_plat_data	*pdata =
		kim_gdata->kim_pdev->dev.platform_data;
	struct tty_struct	*tty = kim_gdata->core_data->tty;

	reinit_completion(&kim_gdata->ldisc_installed);

	if (tty) {	/* can be called before ldisc is installed */
		/* Flush any pending characters in the driver and discipline. */
		tty_ldisc_flush(tty);
		tty_driver_flush_buffer(tty);
	}

	/* send uninstall notification to UIM */
	pr_info("ldisc_install = 0");
	kim_gdata->ldisc_install = 0;
	sysfs_notify(&kim_gdata->kim_pdev->dev.kobj, NULL, "install");

	/* wait for ldisc to be un-installed */
	err = wait_for_completion_interruptible_timeout(
		&kim_gdata->ldisc_installed, msecs_to_jiffies(LDISC_TIME));
	if (!err) {		/* timeout */
		pr_err(" timed out waiting for ldisc to be un-installed");
		err = -ETIMEDOUT;
	}

	/* By default configure BT nShutdown to LOW state */
	gpio_set_value(kim_gdata->nshutdown, GPIO_LOW);
	mdelay(1);
	gpio_set_value(kim_gdata->nshutdown, GPIO_HIGH);
	mdelay(1);
	gpio_set_value(kim_gdata->nshutdown, GPIO_LOW);

	/* platform specific disable */
	if (pdata->chip_disable)
		pdata->chip_disable(kim_gdata);
	return err;
}

/**********************************************************************/
/* functions called from subsystems */
/* called when debugfs entry is read from */

static int show_version(struct seq_file *s, void *unused)
{
	struct kim_data_s *kim_gdata = (struct kim_data_s *)s->private;
	seq_printf(s, "%04X %d.%d.%d\n", kim_gdata->version.full,
			kim_gdata->version.chip, kim_gdata->version.maj_ver,
			kim_gdata->version.min_ver);
	return 0;
}

static int show_list(struct seq_file *s, void *unused)
{
	struct kim_data_s *kim_gdata = (struct kim_data_s *)s->private;
	kim_st_list_protocols(kim_gdata->core_data, s);
	return 0;
}

static ssize_t show_install(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kim_data_s *kim_data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", kim_data->ldisc_install);
}

#ifdef DEBUG
static ssize_t store_dev_name(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kim_data_s *kim_data = dev_get_drvdata(dev);
	pr_debug("storing dev name >%s<", buf);
	strncpy(kim_data->dev_name, buf, count);
	pr_debug("stored dev name >%s<", kim_data->dev_name);
	return count;
}

static ssize_t store_baud_rate(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kim_data_s *kim_data = dev_get_drvdata(dev);
	pr_debug("storing baud rate >%s<", buf);
	sscanf(buf, "%ld", &kim_data->baud_rate);
	pr_debug("stored baud rate >%ld<", kim_data->baud_rate);
	return count;
}
#endif	/* if DEBUG */

static ssize_t show_dev_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kim_data_s *kim_data = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", kim_data->dev_name);
}

static ssize_t show_baud_rate(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kim_data_s *kim_data = dev_get_drvdata(dev);
	return sprintf(buf, "%ld\n", kim_data->baud_rate);
}

static ssize_t show_flow_cntrl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kim_data_s *kim_data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", kim_data->flow_cntrl);
}

/* structures specific for sysfs entries */
static struct kobj_attribute ldisc_install =
__ATTR(install, 0444, (void *)show_install, NULL);

static struct kobj_attribute uart_dev_name =
#ifdef DEBUG	/* TODO: move this to debug-fs if possible */
__ATTR(dev_name, 0644, (void *)show_dev_name, (void *)store_dev_name);
#else
__ATTR(dev_name, 0444, (void *)show_dev_name, NULL);
#endif

static struct kobj_attribute uart_baud_rate =
#ifdef DEBUG	/* TODO: move to debugfs */
__ATTR(baud_rate, 0644, (void *)show_baud_rate, (void *)store_baud_rate);
#else
__ATTR(baud_rate, 0444, (void *)show_baud_rate, NULL);
#endif

static struct kobj_attribute uart_flow_cntrl =
__ATTR(flow_cntrl, 0444, (void *)show_flow_cntrl, NULL);

static struct attribute *uim_attrs[] = {
	&ldisc_install.attr,
	&uart_dev_name.attr,
	&uart_baud_rate.attr,
	&uart_flow_cntrl.attr,
	NULL,
};

static struct attribute_group uim_attr_grp = {
	.attrs = uim_attrs,
};

/**
 * st_kim_ref - reference the core's data
 *	This references the per-ST platform device in the arch/xx/
 *	board-xx.c file.
 *	This would enable multiple such platform devices to exist
 *	on a given platform
 */
void st_kim_ref(struct st_data_s **core_data, int id)
{
	struct platform_device	*pdev;
	struct kim_data_s	*kim_gdata;
	/* get kim_gdata reference from platform device */
	pdev = st_get_plat_device(id);
	if (!pdev) {
		*core_data = NULL;
		return;
	}
	kim_gdata = platform_get_drvdata(pdev);
	*core_data = kim_gdata->core_data;
}

static int kim_version_open(struct inode *i, struct file *f)
{
	return single_open(f, show_version, i->i_private);
}

static int kim_list_open(struct inode *i, struct file *f)
{
	return single_open(f, show_list, i->i_private);
}

static const struct file_operations version_debugfs_fops = {
	/* version info */
	.open = kim_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
static const struct file_operations list_debugfs_fops = {
	/* protocols info */
	.open = kim_list_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/**********************************************************************/
/* functions called from platform device driver subsystem
 * need to have a relevant platform device entry in the platform's
 * board-*.c file
 */

static struct dentry *kim_debugfs_dir;
static int kim_probe(struct platform_device *pdev)
{
	struct kim_data_s	*kim_gdata;
	struct ti_st_plat_data	*pdata = pdev->dev.platform_data;
	int err;

	if ((pdev->id != -1) && (pdev->id < MAX_ST_DEVICES)) {
		/* multiple devices could exist */
		st_kim_devices[pdev->id] = pdev;
	} else {
		/* platform's sure about existence of 1 device */
		st_kim_devices[0] = pdev;
	}

	kim_gdata = kzalloc(sizeof(struct kim_data_s), GFP_ATOMIC);
	if (!kim_gdata) {
		pr_err("no mem to allocate");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, kim_gdata);

	err = st_core_init(&kim_gdata->core_data);
	if (err != 0) {
		pr_err(" ST core init failed");
		err = -EIO;
		goto err_core_init;
	}
	/* refer to itself */
	kim_gdata->core_data->kim_data = kim_gdata;

	/* Claim the chip enable nShutdown gpio from the system */
	kim_gdata->nshutdown = pdata->nshutdown_gpio;
	err = gpio_request(kim_gdata->nshutdown, "kim");
	if (unlikely(err)) {
		pr_err(" gpio %ld request failed ", kim_gdata->nshutdown);
		return err;
	}

	/* Configure nShutdown GPIO as output=0 */
	err = gpio_direction_output(kim_gdata->nshutdown, 0);
	if (unlikely(err)) {
		pr_err(" unable to configure gpio %ld", kim_gdata->nshutdown);
		return err;
	}
	/* get reference of pdev for request_firmware
	 */
	kim_gdata->kim_pdev = pdev;
	init_completion(&kim_gdata->kim_rcvd);
	init_completion(&kim_gdata->ldisc_installed);

	err = sysfs_create_group(&pdev->dev.kobj, &uim_attr_grp);
	if (err) {
		pr_err("failed to create sysfs entries");
		goto err_sysfs_group;
	}

	/* copying platform data */
	strncpy(kim_gdata->dev_name, pdata->dev_name, UART_DEV_NAME_LEN);
	kim_gdata->flow_cntrl = pdata->flow_cntrl;
	kim_gdata->baud_rate = pdata->baud_rate;
	pr_info("sysfs entries created\n");

	kim_debugfs_dir = debugfs_create_dir("ti-st", NULL);
	if (!kim_debugfs_dir) {
		pr_err(" debugfs entries creation failed ");
		err = -EIO;
		goto err_debugfs_dir;
	}

	debugfs_create_file("version", S_IRUGO, kim_debugfs_dir,
				kim_gdata, &version_debugfs_fops);
	debugfs_create_file("protocols", S_IRUGO, kim_debugfs_dir,
				kim_gdata, &list_debugfs_fops);
	return 0;

err_debugfs_dir:
	sysfs_remove_group(&pdev->dev.kobj, &uim_attr_grp);

err_sysfs_group:
	st_core_exit(kim_gdata->core_data);

err_core_init:
	kfree(kim_gdata);

	return err;
}

static int kim_remove(struct platform_device *pdev)
{
	/* free the GPIOs requested */
	struct ti_st_plat_data	*pdata = pdev->dev.platform_data;
	struct kim_data_s	*kim_gdata;

	kim_gdata = platform_get_drvdata(pdev);

	/* Free the Bluetooth/FM/GPIO
	 * nShutdown gpio from the system
	 */
	gpio_free(pdata->nshutdown_gpio);
	pr_info("nshutdown GPIO Freed");

	debugfs_remove_recursive(kim_debugfs_dir);
	sysfs_remove_group(&pdev->dev.kobj, &uim_attr_grp);
	pr_info("sysfs entries removed");

	kim_gdata->kim_pdev = NULL;
	st_core_exit(kim_gdata->core_data);

	kfree(kim_gdata);
	kim_gdata = NULL;
	return 0;
}

static int kim_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ti_st_plat_data	*pdata = pdev->dev.platform_data;

	if (pdata->suspend)
		return pdata->suspend(pdev, state);

	return -EOPNOTSUPP;
}

static int kim_resume(struct platform_device *pdev)
{
	struct ti_st_plat_data	*pdata = pdev->dev.platform_data;

	if (pdata->resume)
		return pdata->resume(pdev);

	return -EOPNOTSUPP;
}

/**********************************************************************/
/* entry point for ST KIM module, called in from ST Core */
static struct platform_driver kim_platform_driver = {
	.probe = kim_probe,
	.remove = kim_remove,
	.suspend = kim_suspend,
	.resume = kim_resume,
	.driver = {
		.name = "kim",
	},
};

module_platform_driver(kim_platform_driver);

MODULE_AUTHOR("Pavan Savoy <pavan_savoy@ti.com>");
MODULE_DESCRIPTION("Shared Transport Driver for TI BT/FM/GPS combo chips ");
MODULE_LICENSE("GPL");
