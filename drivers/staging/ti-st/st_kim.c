/*
 *  Shared Transport Line discipline driver Core
 *	Init Manager module responsible for GPIO control
 *	and firmware download
 *  Copyright (C) 2009 Texas Instruments
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

#include <linux/sched.h>

#include "st_kim.h"
/* understand BT events for fw response */
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>


static int kim_probe(struct platform_device *pdev);
static int kim_remove(struct platform_device *pdev);

/* KIM platform device driver structure */
static struct platform_driver kim_platform_driver = {
	.probe = kim_probe,
	.remove = kim_remove,
	/* TODO: ST driver power management during suspend/resume ?
	 */
#if 0
	.suspend = kim_suspend,
	.resume = kim_resume,
#endif
	.driver = {
		   .name = "kim",
		   .owner = THIS_MODULE,
		   },
};

#ifndef LEGACY_RFKILL_SUPPORT
static ssize_t show_pid(struct device *dev, struct device_attribute
			*attr, char *buf);
static ssize_t store_pid(struct device *dev, struct device_attribute
			 *devattr, char *buf, size_t count);
static ssize_t show_list(struct device *dev, struct device_attribute
			 *attr, char *buf);

/* structures specific for sysfs entries */
static struct kobj_attribute pid_attr =
__ATTR(pid, 0644, (void *)show_pid, (void *)store_pid);

static struct kobj_attribute list_protocols =
__ATTR(protocols, 0444, (void *)show_list, NULL);

static struct attribute *uim_attrs[] = {
	&pid_attr.attr,
	/* add more debug sysfs entries */
	&list_protocols.attr,
	NULL,
};

static struct attribute_group uim_attr_grp = {
	.attrs = uim_attrs,
};
#else
static int kim_toggle_radio(void*, bool);
static const struct rfkill_ops kim_rfkill_ops = {
	.set_block = kim_toggle_radio,
};
#endif	/* LEGACY_RFKILL_SUPPORT */

/* strings to be used for rfkill entries and by
 * ST Core to be used for sysfs debug entry
 */
#define PROTO_ENTRY(type, name)	name
const unsigned char *protocol_names[] = {
	PROTO_ENTRY(ST_BT, "Bluetooth"),
	PROTO_ENTRY(ST_FM, "FM"),
	PROTO_ENTRY(ST_GPS, "GPS"),
};

struct kim_data_s	*kim_gdata;

/**********************************************************************/
/* internal functions */

/*
 * function to return whether the firmware response was proper
 * in case of error don't complete so that waiting for proper
 * response times out
 */
void validate_firmware_response(struct sk_buff *skb)
{
	if (unlikely(skb->data[5] != 0)) {
		pr_err("no proper response during fw download");
		pr_err("data6 %x", skb->data[5]);
		return;		/* keep waiting for the proper response */
	}
	/* becos of all the script being downloaded */
	complete_all(&kim_gdata->kim_rcvd);
	kfree_skb(skb);
}

/* check for data len received inside kim_int_recv
 * most often hit the last case to update state to waiting for data
 */
static inline int kim_check_data_len(int len)
{
	register int room = skb_tailroom(kim_gdata->rx_skb);

	pr_info("len %d room %d", len, room);

	if (!len) {
		validate_firmware_response(kim_gdata->rx_skb);
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
		kim_gdata->rx_state = ST_BT_W4_DATA;
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

/* receive function called during firmware download
 * - firmware download responses on different UART drivers
 *   have been observed to come in bursts of different
 *   tty_receive and hence the logic
 */
void kim_int_recv(const unsigned char *data, long count)
{
	register char *ptr;
	struct hci_event_hdr *eh;
	register int len = 0, type = 0;

	pr_info("%s", __func__);
	/* Decode received bytes here */
	ptr = (char *)data;
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
			case ST_BT_W4_DATA:
				pr_info("Complete pkt received");
				validate_firmware_response(kim_gdata->rx_skb);
				kim_gdata->rx_state = ST_W4_PACKET_TYPE;
				kim_gdata->rx_skb = NULL;
				continue;
				/* Waiting for Bluetooth event header ? */
			case ST_BT_W4_EVENT_HDR:
				eh = (struct hci_event_hdr *)kim_gdata->
				    rx_skb->data;
				pr_info("Event header: evt 0x%2.2x"
					   "plen %d", eh->evt, eh->plen);
				kim_check_data_len(eh->plen);
				continue;
			}	/* end of switch */
		}		/* end of if rx_state */
		switch (*ptr) {
			/* Bluetooth event packet? */
		case HCI_EVENT_PKT:
			pr_info("Event packet");
			kim_gdata->rx_state = ST_BT_W4_EVENT_HDR;
			kim_gdata->rx_count = HCI_EVENT_HDR_SIZE;
			type = HCI_EVENT_PKT;
			break;
		default:
			pr_info("unknown packet");
			ptr++;
			count--;
			continue;
		}		/* end of switch *ptr */
		ptr++;
		count--;
		kim_gdata->rx_skb =
		    bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
		if (!kim_gdata->rx_skb) {
			pr_err("can't allocate mem for new packet");
			kim_gdata->rx_state = ST_W4_PACKET_TYPE;
			kim_gdata->rx_count = 0;
			return;
		} /* not necessary in this case */
		bt_cb(kim_gdata->rx_skb)->pkt_type = type;
	}			/* end of while count */
	pr_info("done %s", __func__);
	return;
}

static long read_local_version(char *bts_scr_name)
{
	unsigned short version = 0, chip = 0, min_ver = 0, maj_ver = 0;
	char read_ver_cmd[] = { 0x01, 0x01, 0x10, 0x00 };

	pr_info("%s", __func__);

	INIT_COMPLETION(kim_gdata->kim_rcvd);
	if (4 != st_int_write(kim_gdata->core_data, read_ver_cmd, 4)) {
		pr_err("kim: couldn't write 4 bytes");
		return ST_ERR_FAILURE;
	}

	if (!wait_for_completion_timeout
	    (&kim_gdata->kim_rcvd, msecs_to_jiffies(CMD_RESP_TIME))) {
		pr_err(" waiting for ver info- timed out ");
		return ST_ERR_FAILURE;
	}

	version =
	    MAKEWORD(kim_gdata->resp_buffer[13], kim_gdata->resp_buffer[14]);
	chip = (version & 0x7C00) >> 10;
	min_ver = (version & 0x007F);
	maj_ver = (version & 0x0380) >> 7;

	if (version & 0x8000)
		maj_ver |= 0x0008;

	sprintf(bts_scr_name, "TIInit_%d.%d.%d.bts", chip, maj_ver, min_ver);
	pr_info("%s", bts_scr_name);
	return ST_SUCCESS;
}

/* internal function which parses through the .bts firmware script file
 * intreprets SEND, DELAY actions only as of now
 */
static long download_firmware(void)
{
	long err = ST_SUCCESS;
	long len = 0;
	register unsigned char *ptr = NULL;
	register unsigned char *action_ptr = NULL;
	unsigned char bts_scr_name[30] = { 0 };	/* 30 char long bts scr name? */

	pr_info("%s", __func__);

	err = read_local_version(bts_scr_name);
	if (err != ST_SUCCESS) {
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
		return ST_ERR_FAILURE;
	}
	ptr = (void *)kim_gdata->fw_entry->data;
	len = kim_gdata->fw_entry->size;
	/* bts_header to remove out magic number and
	 * version
	 */
	ptr += sizeof(struct bts_header);
	len -= sizeof(struct bts_header);

	while (len > 0 && ptr) {
		pr_info(" action size %d, type %d ",
			   ((struct bts_action *)ptr)->size,
			   ((struct bts_action *)ptr)->type);

		switch (((struct bts_action *)ptr)->type) {
		case ACTION_SEND_COMMAND:	/* action send */
			action_ptr = &(((struct bts_action *)ptr)->data[0]);
			if (unlikely
			    (((struct hci_command *)action_ptr)->opcode ==
			     0xFF36)) {
				/* ignore remote change
				 * baud rate HCI VS command */
				pr_err
				    (" change remote baud\
				    rate command in firmware");
				break;
			}

			INIT_COMPLETION(kim_gdata->kim_rcvd);
			err = st_int_write(kim_gdata->core_data,
			((struct bts_action_send *)action_ptr)->data,
					   ((struct bts_action *)ptr)->size);
			if (unlikely(err < 0)) {
				release_firmware(kim_gdata->fw_entry);
				return ST_ERR_FAILURE;
			}
			if (!wait_for_completion_timeout
			    (&kim_gdata->kim_rcvd,
			     msecs_to_jiffies(CMD_RESP_TIME))) {
				pr_err
				    (" response timeout during fw download ");
				/* timed out */
				release_firmware(kim_gdata->fw_entry);
				return ST_ERR_FAILURE;
			}
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
	return ST_SUCCESS;
}

/**********************************************************************/
/* functions called from ST core */

/* function to toggle the GPIO
 * needs to know whether the GPIO is active high or active low
 */
void st_kim_chip_toggle(enum proto_type type, enum kim_gpio_state state)
{
	pr_info(" %s ", __func__);

	if (kim_gdata->gpios[type] == -1) {
		pr_info(" gpio not requested for protocol %s",
			   protocol_names[type]);
		return;
	}
	switch (type) {
	case ST_BT:
		/*Do Nothing */
		break;

	case ST_FM:
		if (state == KIM_GPIO_ACTIVE)
			gpio_set_value(kim_gdata->gpios[ST_FM], GPIO_LOW);
		else
			gpio_set_value(kim_gdata->gpios[ST_FM], GPIO_HIGH);
		break;

	case ST_GPS:
		if (state == KIM_GPIO_ACTIVE)
			gpio_set_value(kim_gdata->gpios[ST_GPS], GPIO_HIGH);
		else
			gpio_set_value(kim_gdata->gpios[ST_GPS], GPIO_LOW);
		break;

	case ST_MAX:
	default:
		break;
	}

	return;
}

/* called from ST Core, when REG_IN_PROGRESS (registration in progress)
 * can be because of
 * 1. response to read local version
 * 2. during send/recv's of firmware download
 */
void st_kim_recv(void *disc_data, const unsigned char *data, long count)
{
	pr_info(" %s ", __func__);
	/* copy to local buffer */
	if (unlikely(data[4] == 0x01 && data[5] == 0x10 && data[0] == 0x04)) {
		/* must be the read_ver_cmd */
		memcpy(kim_gdata->resp_buffer, data, count);
		complete_all(&kim_gdata->kim_rcvd);
		return;
	} else {
		kim_int_recv(data, count);
		/* either completes or times out */
	}
	return;
}

/* to signal completion of line discipline installation
 * called from ST Core, upon tty_open
 */
void st_kim_complete(void)
{
	complete(&kim_gdata->ldisc_installed);
}

/* called from ST Core upon 1st registration
*/
long st_kim_start(void)
{
	long err = ST_SUCCESS;
	long retry = POR_RETRY_COUNT;
	pr_info(" %s", __func__);

	do {
#ifdef LEGACY_RFKILL_SUPPORT
		/* TODO: this is only because rfkill sub-system
		 * doesn't send events to user-space if the state
		 * isn't changed
		 */
		rfkill_set_hw_state(kim_gdata->rfkill[ST_BT], 1);
#endif
		/* Configure BT nShutdown to HIGH state */
		gpio_set_value(kim_gdata->gpios[ST_BT], GPIO_LOW);
		mdelay(5);	/* FIXME: a proper toggle */
		gpio_set_value(kim_gdata->gpios[ST_BT], GPIO_HIGH);
		mdelay(100);
		/* re-initialize the completion */
		INIT_COMPLETION(kim_gdata->ldisc_installed);
#ifndef LEGACY_RFKILL_SUPPORT
		/* send signal to UIM */
		err = kill_pid(find_get_pid(kim_gdata->uim_pid), SIGUSR2, 0);
		if (err != 0) {
			pr_info(" sending SIGUSR2 to uim failed %ld", err);
			err = ST_ERR_FAILURE;
			continue;
		}
#else
		/* unblock and send event to UIM via /dev/rfkill */
		rfkill_set_hw_state(kim_gdata->rfkill[ST_BT], 0);
#endif
		/* wait for ldisc to be installed */
		err = wait_for_completion_timeout(&kim_gdata->ldisc_installed,
				msecs_to_jiffies(LDISC_TIME));
		if (!err) {	/* timeout */
			pr_err("line disc installation timed out ");
			err = ST_ERR_FAILURE;
			continue;
		} else {
			/* ldisc installed now */
			pr_info(" line discipline installed ");
			err = download_firmware();
			if (err != ST_SUCCESS) {
				pr_err("download firmware failed");
				continue;
			} else {	/* on success don't retry */
				break;
			}
		}
	} while (retry--);
	return err;
}

/* called from ST Core, on the last un-registration
*/
long st_kim_stop(void)
{
	long err = ST_SUCCESS;

	INIT_COMPLETION(kim_gdata->ldisc_installed);
#ifndef LEGACY_RFKILL_SUPPORT
	/* send signal to UIM */
	err = kill_pid(find_get_pid(kim_gdata->uim_pid), SIGUSR2, 1);
	if (err != 0) {
		pr_err("sending SIGUSR2 to uim failed %ld", err);
		return ST_ERR_FAILURE;
	}
#else
	/* set BT rfkill to be blocked */
	err = rfkill_set_hw_state(kim_gdata->rfkill[ST_BT], 1);
#endif

	/* wait for ldisc to be un-installed */
	err = wait_for_completion_timeout(&kim_gdata->ldisc_installed,
			msecs_to_jiffies(LDISC_TIME));
	if (!err) {		/* timeout */
		pr_err(" timed out waiting for ldisc to be un-installed");
		return ST_ERR_FAILURE;
	}

	/* By default configure BT nShutdown to LOW state */
	gpio_set_value(kim_gdata->gpios[ST_BT], GPIO_LOW);
	mdelay(1);
	gpio_set_value(kim_gdata->gpios[ST_BT], GPIO_HIGH);
	mdelay(1);
	gpio_set_value(kim_gdata->gpios[ST_BT], GPIO_LOW);
	return err;
}

/**********************************************************************/
/* functions called from subsystems */

#ifndef LEGACY_RFKILL_SUPPORT
/* called when sysfs entry is written to */
static ssize_t store_pid(struct device *dev, struct device_attribute
			 *devattr, char *buf, size_t count)
{
	pr_info("%s: pid %s ", __func__, buf);
	sscanf(buf, "%ld", &kim_gdata->uim_pid);
	/* to be made use by kim_start to signal SIGUSR2
	 */
	return strlen(buf);
}

/* called when sysfs entry is read from */
static ssize_t show_pid(struct device *dev, struct device_attribute
			*attr, char *buf)
{
	sprintf(buf, "%ld", kim_gdata->uim_pid);
	return strlen(buf);
}

/* called when sysfs entry is read from */
static ssize_t show_list(struct device *dev, struct device_attribute
			 *attr, char *buf)
{
	kim_st_list_protocols(kim_gdata->core_data, buf);
	return strlen(buf);
}

#else /* LEGACY_RFKILL_SUPPORT */

/* function called from rfkill subsystem, when someone from
 * user space would write 0/1 on the sysfs entry
 * /sys/class/rfkill/rfkill0,1,3/state
 */
static int kim_toggle_radio(void *data, bool blocked)
{
	enum proto_type type = *((enum proto_type *)data);
	pr_info(" %s: %d ", __func__, type);

	switch (type) {
	case ST_BT:
		/* do nothing */
	break;
	case ST_FM:
	case ST_GPS:
		if (blocked)
			st_kim_chip_toggle(type, KIM_GPIO_INACTIVE);
		else
			st_kim_chip_toggle(type, KIM_GPIO_ACTIVE);
	break;
	case ST_MAX:
		pr_err(" wrong proto type ");
	break;
	}
	return ST_SUCCESS;
}

#endif	/* LEGACY_RFKILL_SUPPORT */

void st_kim_ref(struct st_data_s **core_data)
{
	*core_data = kim_gdata->core_data;
}

/**********************************************************************/
/* functions called from platform device driver subsystem
 * need to have a relevant platform device entry in the platform's
 * board-*.c file
 */

static int kim_probe(struct platform_device *pdev)
{
	long status;
	long proto;
	long *gpios = pdev->dev.platform_data;

	status = st_core_init(&kim_gdata->core_data);
	if (status != 0) {
		pr_err(" ST core init failed");
		return ST_ERR_FAILURE;
	}

	for (proto = 0; proto < ST_MAX; proto++) {
		kim_gdata->gpios[proto] = gpios[proto];
		pr_info(" %ld gpio to be requested", gpios[proto]);
	}

	for (proto = 0; (proto < ST_MAX) && (gpios[proto] != -1); proto++) {
		/* Claim the Bluetooth/FM/GPIO
		 * nShutdown gpio from the system
		 */
		status = gpio_request(gpios[proto], "kim");
		if (unlikely(status)) {
			pr_err(" gpio %ld request failed ", gpios[proto]);
			proto -= 1;
			while (proto >= 0) {
				if (gpios[proto] != -1)
					gpio_free(gpios[proto]);
			}
			return status;
		}

		/* Configure nShutdown GPIO as output=0 */
		status =
		    gpio_direction_output(gpios[proto], 0);
		if (unlikely(status)) {
			pr_err(" unable to configure gpio %ld",
				   gpios[proto]);
			proto -= 1;
			while (proto >= 0) {
				if (gpios[proto] != -1)
					gpio_free(gpios[proto]);
			}
			return status;
		}
	}
#ifndef LEGACY_RFKILL_SUPPORT
	/* pdev to contain BT, FM and GPS enable/N-Shutdown GPIOs
	 * execute request_gpio, set output direction
	 */
	kim_gdata->kim_kobj = kobject_create_and_add("uim", NULL);
	/* create the sysfs entry for UIM to put in pid */
	if (sysfs_create_group(kim_gdata->kim_kobj, &uim_attr_grp)) {
		pr_err(" sysfs entry creation failed");
		kobject_put(kim_gdata->kim_kobj);
		/* free requested GPIOs and fail probe */
		for (proto = ST_BT; proto < ST_MAX; proto++) {
			if (gpios[proto] != -1)
				gpio_free(gpios[proto]);
		}
		return -1;	/* fail insmod */
	}
	pr_info(" sysfs entry created ");
#endif
	/* get reference of pdev for request_firmware
	 */
	kim_gdata->kim_pdev = pdev;
	init_completion(&kim_gdata->kim_rcvd);
	init_completion(&kim_gdata->ldisc_installed);
#ifdef LEGACY_RFKILL_SUPPORT
	for (proto = 0; (proto < ST_MAX) && (gpios[proto] != -1); proto++) {
		/* TODO: should all types be rfkill_type_bt ? */
		kim_gdata->rf_protos[proto] = proto;
		kim_gdata->rfkill[proto] = rfkill_alloc(protocol_names[proto],
			&pdev->dev, RFKILL_TYPE_BLUETOOTH,
			&kim_rfkill_ops, &kim_gdata->rf_protos[proto]);
		if (kim_gdata->rfkill[proto] == NULL) {
			pr_err("cannot create rfkill entry for gpio %ld",
				   gpios[proto]);
			continue;
		}
		/* block upon creation */
		rfkill_init_sw_state(kim_gdata->rfkill[proto], 1);
		status = rfkill_register(kim_gdata->rfkill[proto]);
		if (unlikely(status)) {
			pr_err("rfkill registration failed for gpio %ld",
				   gpios[proto]);
			rfkill_unregister(kim_gdata->rfkill[proto]);
			continue;
		}
		pr_info("rfkill entry created for %ld", gpios[proto]);
	}
#endif
	return ST_SUCCESS;
}

static int kim_remove(struct platform_device *pdev)
{
	/* free the GPIOs requested
	 */
	long *gpios = pdev->dev.platform_data;
	long proto;

	for (proto = 0; (proto < ST_MAX) && (gpios[proto] != -1); proto++) {
		/* Claim the Bluetooth/FM/GPIO
		 * nShutdown gpio from the system
		 */
		gpio_free(gpios[proto]);
#ifdef LEGACY_RFKILL_SUPPORT
		rfkill_unregister(kim_gdata->rfkill[proto]);
		rfkill_destroy(kim_gdata->rfkill[proto]);
		kim_gdata->rfkill[proto] = NULL;
#endif
	}
	pr_info("kim: GPIO Freed");
#ifndef LEGACY_RFKILL_SUPPORT
	/* delete the sysfs entries */
	sysfs_remove_group(kim_gdata->kim_kobj, &uim_attr_grp);
	kobject_put(kim_gdata->kim_kobj);
#endif
	kim_gdata->kim_pdev = NULL;
	st_core_exit(kim_gdata->core_data);
	return ST_SUCCESS;
}

/**********************************************************************/
/* entry point for ST KIM module, called in from ST Core */

static int __init st_kim_init(void)
{
	long ret = ST_SUCCESS;
	kim_gdata = kzalloc(sizeof(struct kim_data_s), GFP_ATOMIC);
	if (!kim_gdata) {
		pr_err("no mem to allocate");
		return -ENOMEM;
	}

	ret = platform_driver_register(&kim_platform_driver);
	if (ret != 0) {
		pr_err("platform drv registration failed");
		return ST_ERR_FAILURE;
	}
	return ST_SUCCESS;
}

static void __exit st_kim_deinit(void)
{
	/* the following returns void */
	platform_driver_unregister(&kim_platform_driver);
	kfree(kim_gdata);
	kim_gdata = NULL;
}


module_init(st_kim_init);
module_exit(st_kim_deinit);
MODULE_AUTHOR("Pavan Savoy <pavan_savoy@ti.com>");
MODULE_DESCRIPTION("Shared Transport Driver for TI BT/FM/GPS combo chips ");
MODULE_LICENSE("GPL");
