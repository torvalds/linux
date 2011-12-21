/*
 * Remote VUB300 SDIO/SDmem Host Controller Driver
 *
 * Copyright (C) 2010 Elan Digital Systems Limited
 *
 * based on USB Skeleton driver - 2.2
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2
 *
 * VUB300: is a USB 2.0 client device with a single SDIO/SDmem/MMC slot
 *         Any SDIO/SDmem/MMC device plugged into the VUB300 will appear,
 *         by virtue of this driver, to have been plugged into a local
 *         SDIO host controller, similar to, say, a PCI Ricoh controller
 *         This is because this kernel device driver is both a USB 2.0
 *         client device driver AND an MMC host controller driver. Thus
 *         if there is an existing driver for the inserted SDIO/SDmem/MMC
 *         device then that driver will be used by the kernel to manage
 *         the device in exactly the same fashion as if it had been
 *         directly plugged into, say, a local pci bus Ricoh controller
 *
 * RANT: this driver was written using a display 128x48 - converting it
 *       to a line width of 80 makes it very difficult to support. In
 *       particular functions have been broken down into sub functions
 *       and the original meaningful names have been shortened into
 *       cryptic ones.
 *       The problem is that executing a fragment of code subject to
 *       two conditions means an indentation of 24, thus leaving only
 *       56 characters for a C statement. And that is quite ridiculous!
 *
 * Data types: data passed to/from the VUB300 is fixed to a number of
 *             bits and driver data fields reflect that limit by using
 *             u8, u16, u32
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/workqueue.h>
#include <linux/ctype.h>
#include <linux/firmware.h>
#include <linux/scatterlist.h>

struct host_controller_info {
	u8 info_size;
	u16 firmware_version;
	u8 number_of_ports;
} __packed;

#define FIRMWARE_BLOCK_BOUNDARY 1024
struct sd_command_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
	u8 command_type; /* Bit7 - Rd/Wr */
	u8 command_index;
	u8 transfer_size[4]; /* ReadSize + ReadSize */
	u8 response_type;
	u8 arguments[4];
	u8 block_count[2];
	u8 block_size[2];
	u8 block_boundary[2];
	u8 reserved[44]; /* to pad out to 64 bytes */
} __packed;

struct sd_irqpoll_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
	u8 command_type; /* Bit7 - Rd/Wr */
	u8 padding[16]; /* don't ask why !! */
	u8 poll_timeout_msb;
	u8 poll_timeout_lsb;
	u8 reserved[42]; /* to pad out to 64 bytes */
} __packed;

struct sd_common_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
} __packed;

struct sd_response_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
	u8 command_type;
	u8 command_index;
	u8 command_response[0];
} __packed;

struct sd_status_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
	u16 port_flags;
	u32 sdio_clock;
	u16 host_header_size;
	u16 func_header_size;
	u16 ctrl_header_size;
} __packed;

struct sd_error_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
	u8 error_code;
} __packed;

struct sd_interrupt_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
} __packed;

struct offload_registers_access {
	u8 command_byte[4];
	u8 Respond_Byte[4];
} __packed;

#define INTERRUPT_REGISTER_ACCESSES 15
struct sd_offloaded_interrupt {
	u8 header_size;
	u8 header_type;
	u8 port_number;
	struct offload_registers_access reg[INTERRUPT_REGISTER_ACCESSES];
} __packed;

struct sd_register_header {
	u8 header_size;
	u8 header_type;
	u8 port_number;
	u8 command_type;
	u8 command_index;
	u8 command_response[6];
} __packed;

#define PIGGYBACK_REGISTER_ACCESSES 14
struct sd_offloaded_piggyback {
	struct sd_register_header sdio;
	struct offload_registers_access reg[PIGGYBACK_REGISTER_ACCESSES];
} __packed;

union sd_response {
	struct sd_common_header common;
	struct sd_status_header status;
	struct sd_error_header error;
	struct sd_interrupt_header interrupt;
	struct sd_response_header response;
	struct sd_offloaded_interrupt irq;
	struct sd_offloaded_piggyback pig;
} __packed;

union sd_command {
	struct sd_command_header head;
	struct sd_irqpoll_header poll;
} __packed;

enum SD_RESPONSE_TYPE {
	SDRT_UNSPECIFIED = 0,
	SDRT_NONE,
	SDRT_1,
	SDRT_1B,
	SDRT_2,
	SDRT_3,
	SDRT_4,
	SDRT_5,
	SDRT_5B,
	SDRT_6,
	SDRT_7,
};

#define RESPONSE_INTERRUPT			0x01
#define RESPONSE_ERROR				0x02
#define RESPONSE_STATUS				0x03
#define RESPONSE_IRQ_DISABLED			0x05
#define RESPONSE_IRQ_ENABLED			0x06
#define RESPONSE_PIGGYBACKED			0x07
#define RESPONSE_NO_INTERRUPT			0x08
#define RESPONSE_PIG_DISABLED			0x09
#define RESPONSE_PIG_ENABLED			0x0A
#define SD_ERROR_1BIT_TIMEOUT			0x01
#define SD_ERROR_4BIT_TIMEOUT			0x02
#define SD_ERROR_1BIT_CRC_WRONG			0x03
#define SD_ERROR_4BIT_CRC_WRONG			0x04
#define SD_ERROR_1BIT_CRC_ERROR			0x05
#define SD_ERROR_4BIT_CRC_ERROR			0x06
#define SD_ERROR_NO_CMD_ENDBIT			0x07
#define SD_ERROR_NO_1BIT_DATEND			0x08
#define SD_ERROR_NO_4BIT_DATEND			0x09
#define SD_ERROR_1BIT_UNEXPECTED_TIMEOUT	0x0A
#define SD_ERROR_4BIT_UNEXPECTED_TIMEOUT	0x0B
#define SD_ERROR_ILLEGAL_COMMAND		0x0C
#define SD_ERROR_NO_DEVICE			0x0D
#define SD_ERROR_TRANSFER_LENGTH		0x0E
#define SD_ERROR_1BIT_DATA_TIMEOUT		0x0F
#define SD_ERROR_4BIT_DATA_TIMEOUT		0x10
#define SD_ERROR_ILLEGAL_STATE			0x11
#define SD_ERROR_UNKNOWN_ERROR			0x12
#define SD_ERROR_RESERVED_ERROR			0x13
#define SD_ERROR_INVALID_FUNCTION		0x14
#define SD_ERROR_OUT_OF_RANGE			0x15
#define SD_ERROR_STAT_CMD			0x16
#define SD_ERROR_STAT_DATA			0x17
#define SD_ERROR_STAT_CMD_TIMEOUT		0x18
#define SD_ERROR_SDCRDY_STUCK			0x19
#define SD_ERROR_UNHANDLED			0x1A
#define SD_ERROR_OVERRUN			0x1B
#define SD_ERROR_PIO_TIMEOUT			0x1C

#define FUN(c) (0x000007 & (c->arg>>28))
#define REG(c) (0x01FFFF & (c->arg>>9))

static int limit_speed_to_24_MHz;
module_param(limit_speed_to_24_MHz, bool, 0644);
MODULE_PARM_DESC(limit_speed_to_24_MHz, "Limit Max SDIO Clock Speed to 24 MHz");

static int pad_input_to_usb_pkt;
module_param(pad_input_to_usb_pkt, bool, 0644);
MODULE_PARM_DESC(pad_input_to_usb_pkt,
		 "Pad USB data input transfers to whole USB Packet");

static int disable_offload_processing;
module_param(disable_offload_processing, bool, 0644);
MODULE_PARM_DESC(disable_offload_processing, "Disable Offload Processing");

static int force_1_bit_data_xfers;
module_param(force_1_bit_data_xfers, bool, 0644);
MODULE_PARM_DESC(force_1_bit_data_xfers,
		 "Force SDIO Data Transfers to 1-bit Mode");

static int force_polling_for_irqs;
module_param(force_polling_for_irqs, bool, 0644);
MODULE_PARM_DESC(force_polling_for_irqs, "Force Polling for SDIO interrupts");

static int firmware_irqpoll_timeout = 1024;
module_param(firmware_irqpoll_timeout, int, 0644);
MODULE_PARM_DESC(firmware_irqpoll_timeout, "VUB300 firmware irqpoll timeout");

static int force_max_req_size = 128;
module_param(force_max_req_size, int, 0644);
MODULE_PARM_DESC(force_max_req_size, "set max request size in kBytes");

#ifdef SMSC_DEVELOPMENT_BOARD
static int firmware_rom_wait_states = 0x04;
#else
static int firmware_rom_wait_states = 0x1C;
#endif

module_param(firmware_rom_wait_states, int, 0644);
MODULE_PARM_DESC(firmware_rom_wait_states,
		 "ROM wait states byte=RRRIIEEE (Reserved Internal External)");

#define ELAN_VENDOR_ID		0x2201
#define VUB300_VENDOR_ID	0x0424
#define VUB300_PRODUCT_ID	0x012C
static struct usb_device_id vub300_table[] = {
	{USB_DEVICE(ELAN_VENDOR_ID, VUB300_PRODUCT_ID)},
	{USB_DEVICE(VUB300_VENDOR_ID, VUB300_PRODUCT_ID)},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, vub300_table);

static struct workqueue_struct *cmndworkqueue;
static struct workqueue_struct *pollworkqueue;
static struct workqueue_struct *deadworkqueue;

static inline int interface_to_InterfaceNumber(struct usb_interface *interface)
{
	if (!interface)
		return -1;
	if (!interface->cur_altsetting)
		return -1;
	return interface->cur_altsetting->desc.bInterfaceNumber;
}

struct sdio_register {
	unsigned func_num:3;
	unsigned sdio_reg:17;
	unsigned activate:1;
	unsigned prepared:1;
	unsigned regvalue:8;
	unsigned response:8;
	unsigned sparebit:26;
};

struct vub300_mmc_host {
	struct usb_device *udev;
	struct usb_interface *interface;
	struct kref kref;
	struct mutex cmd_mutex;
	struct mutex irq_mutex;
	char vub_name[3 + (9 * 8) + 4 + 1]; /* max of 7 sdio fn's */
	u8 cmnd_out_ep; /* EndPoint for commands */
	u8 cmnd_res_ep; /* EndPoint for responses */
	u8 data_out_ep; /* EndPoint for out data */
	u8 data_inp_ep; /* EndPoint for inp data */
	bool card_powered;
	bool card_present;
	bool read_only;
	bool large_usb_packets;
	bool app_spec; /* ApplicationSpecific */
	bool irq_enabled; /* by the MMC CORE */
	bool irq_disabled; /* in the firmware */
	unsigned bus_width:4;
	u8 total_offload_count;
	u8 dynamic_register_count;
	u8 resp_len;
	u32 datasize;
	int errors;
	int usb_transport_fail;
	int usb_timed_out;
	int irqs_queued;
	struct sdio_register sdio_register[16];
	struct offload_interrupt_function_register {
#define MAXREGBITS 4
#define MAXREGS (1<<MAXREGBITS)
#define MAXREGMASK (MAXREGS-1)
		u8 offload_count;
		u32 offload_point;
		struct offload_registers_access reg[MAXREGS];
	} fn[8];
	u16 fbs[8]; /* Function Block Size */
	struct mmc_command *cmd;
	struct mmc_request *req;
	struct mmc_data *data;
	struct mmc_host *mmc;
	struct urb *urb;
	struct urb *command_out_urb;
	struct urb *command_res_urb;
	struct completion command_complete;
	struct completion irqpoll_complete;
	union sd_command cmnd;
	union sd_response resp;
	struct timer_list sg_transfer_timer;
	struct usb_sg_request sg_request;
	struct timer_list inactivity_timer;
	struct work_struct deadwork;
	struct work_struct cmndwork;
	struct delayed_work pollwork;
	struct host_controller_info hc_info;
	struct sd_status_header system_port_status;
	u8 padded_buffer[64];
};

#define kref_to_vub300_mmc_host(d) container_of(d, struct vub300_mmc_host, kref)
#define SET_TRANSFER_PSEUDOCODE		21
#define SET_INTERRUPT_PSEUDOCODE	20
#define SET_FAILURE_MODE		18
#define SET_ROM_WAIT_STATES		16
#define SET_IRQ_ENABLE			13
#define SET_CLOCK_SPEED			11
#define SET_FUNCTION_BLOCK_SIZE		9
#define SET_SD_DATA_MODE		6
#define SET_SD_POWER			4
#define ENTER_DFU_MODE			3
#define GET_HC_INF0			1
#define GET_SYSTEM_PORT_STATUS		0

static void vub300_delete(struct kref *kref)
{				/* kref callback - softirq */
	struct vub300_mmc_host *vub300 = kref_to_vub300_mmc_host(kref);
	struct mmc_host *mmc = vub300->mmc;
	usb_free_urb(vub300->command_out_urb);
	vub300->command_out_urb = NULL;
	usb_free_urb(vub300->command_res_urb);
	vub300->command_res_urb = NULL;
	usb_put_dev(vub300->udev);
	mmc_free_host(mmc);
	/*
	 * and hence also frees vub300
	 * which is contained at the end of struct mmc
	 */
}

static void vub300_queue_cmnd_work(struct vub300_mmc_host *vub300)
{
	kref_get(&vub300->kref);
	if (queue_work(cmndworkqueue, &vub300->cmndwork)) {
		/*
		 * then the cmndworkqueue was not previously
		 * running and the above get ref is obvious
		 * required and will be put when the thread
		 * terminates by a specific call
		 */
	} else {
		/*
		 * the cmndworkqueue was already running from
		 * a previous invocation and thus to keep the
		 * kref counts correct we must undo the get
		 */
		kref_put(&vub300->kref, vub300_delete);
	}
}

static void vub300_queue_poll_work(struct vub300_mmc_host *vub300, int delay)
{
	kref_get(&vub300->kref);
	if (queue_delayed_work(pollworkqueue, &vub300->pollwork, delay)) {
		/*
		 * then the pollworkqueue was not previously
		 * running and the above get ref is obvious
		 * required and will be put when the thread
		 * terminates by a specific call
		 */
	} else {
		/*
		 * the pollworkqueue was already running from
		 * a previous invocation and thus to keep the
		 * kref counts correct we must undo the get
		 */
		kref_put(&vub300->kref, vub300_delete);
	}
}

static void vub300_queue_dead_work(struct vub300_mmc_host *vub300)
{
	kref_get(&vub300->kref);
	if (queue_work(deadworkqueue, &vub300->deadwork)) {
		/*
		 * then the deadworkqueue was not previously
		 * running and the above get ref is obvious
		 * required and will be put when the thread
		 * terminates by a specific call
		 */
	} else {
		/*
		 * the deadworkqueue was already running from
		 * a previous invocation and thus to keep the
		 * kref counts correct we must undo the get
		 */
		kref_put(&vub300->kref, vub300_delete);
	}
}

static void irqpoll_res_completed(struct urb *urb)
{				/* urb completion handler - hardirq */
	struct vub300_mmc_host *vub300 = (struct vub300_mmc_host *)urb->context;
	if (urb->status)
		vub300->usb_transport_fail = urb->status;
	complete(&vub300->irqpoll_complete);
}

static void irqpoll_out_completed(struct urb *urb)
{				/* urb completion handler - hardirq */
	struct vub300_mmc_host *vub300 = (struct vub300_mmc_host *)urb->context;
	if (urb->status) {
		vub300->usb_transport_fail = urb->status;
		complete(&vub300->irqpoll_complete);
		return;
	} else {
		int ret;
		unsigned int pipe =
			usb_rcvbulkpipe(vub300->udev, vub300->cmnd_res_ep);
		usb_fill_bulk_urb(vub300->command_res_urb, vub300->udev, pipe,
				  &vub300->resp, sizeof(vub300->resp),
				  irqpoll_res_completed, vub300);
		vub300->command_res_urb->actual_length = 0;
		ret = usb_submit_urb(vub300->command_res_urb, GFP_ATOMIC);
		if (ret) {
			vub300->usb_transport_fail = ret;
			complete(&vub300->irqpoll_complete);
		}
		return;
	}
}

static void send_irqpoll(struct vub300_mmc_host *vub300)
{
	/* cmd_mutex is held by vub300_pollwork_thread */
	int retval;
	int timeout = 0xFFFF & (0x0001FFFF - firmware_irqpoll_timeout);
	vub300->cmnd.poll.header_size = 22;
	vub300->cmnd.poll.header_type = 1;
	vub300->cmnd.poll.port_number = 0;
	vub300->cmnd.poll.command_type = 2;
	vub300->cmnd.poll.poll_timeout_lsb = 0xFF & (unsigned)timeout;
	vub300->cmnd.poll.poll_timeout_msb = 0xFF & (unsigned)(timeout >> 8);
	usb_fill_bulk_urb(vub300->command_out_urb, vub300->udev,
			  usb_sndbulkpipe(vub300->udev, vub300->cmnd_out_ep)
			  , &vub300->cmnd, sizeof(vub300->cmnd)
			  , irqpoll_out_completed, vub300);
	retval = usb_submit_urb(vub300->command_out_urb, GFP_KERNEL);
	if (0 > retval) {
		vub300->usb_transport_fail = retval;
		vub300_queue_poll_work(vub300, 1);
		complete(&vub300->irqpoll_complete);
		return;
	} else {
		return;
	}
}

static void new_system_port_status(struct vub300_mmc_host *vub300)
{
	int old_card_present = vub300->card_present;
	int new_card_present =
		(0x0001 & vub300->system_port_status.port_flags) ? 1 : 0;
	vub300->read_only =
		(0x0010 & vub300->system_port_status.port_flags) ? 1 : 0;
	if (new_card_present && !old_card_present) {
		dev_info(&vub300->udev->dev, "card just inserted\n");
		vub300->card_present = 1;
		vub300->bus_width = 0;
		if (disable_offload_processing)
			strncpy(vub300->vub_name, "EMPTY Processing Disabled",
				sizeof(vub300->vub_name));
		else
			vub300->vub_name[0] = 0;
		mmc_detect_change(vub300->mmc, 1);
	} else if (!new_card_present && old_card_present) {
		dev_info(&vub300->udev->dev, "card just ejected\n");
		vub300->card_present = 0;
		mmc_detect_change(vub300->mmc, 0);
	} else {
		/* no change */
	}
}

static void __add_offloaded_reg_to_fifo(struct vub300_mmc_host *vub300,
					struct offload_registers_access
					*register_access, u8 func)
{
	u8 r = vub300->fn[func].offload_point + vub300->fn[func].offload_count;
	memcpy(&vub300->fn[func].reg[MAXREGMASK & r], register_access,
	       sizeof(struct offload_registers_access));
	vub300->fn[func].offload_count += 1;
	vub300->total_offload_count += 1;
}

static void add_offloaded_reg(struct vub300_mmc_host *vub300,
			      struct offload_registers_access *register_access)
{
	u32 Register = ((0x03 & register_access->command_byte[0]) << 15)
			| ((0xFF & register_access->command_byte[1]) << 7)
			| ((0xFE & register_access->command_byte[2]) >> 1);
	u8 func = ((0x70 & register_access->command_byte[0]) >> 4);
	u8 regs = vub300->dynamic_register_count;
	u8 i = 0;
	while (0 < regs-- && 1 == vub300->sdio_register[i].activate) {
		if (vub300->sdio_register[i].func_num == func &&
		    vub300->sdio_register[i].sdio_reg == Register) {
			if (vub300->sdio_register[i].prepared == 0)
				vub300->sdio_register[i].prepared = 1;
			vub300->sdio_register[i].response =
				register_access->Respond_Byte[2];
			vub300->sdio_register[i].regvalue =
				register_access->Respond_Byte[3];
			return;
		} else {
			i += 1;
			continue;
		}
	};
	__add_offloaded_reg_to_fifo(vub300, register_access, func);
}

static void check_vub300_port_status(struct vub300_mmc_host *vub300)
{
	/*
	 * cmd_mutex is held by vub300_pollwork_thread,
	 * vub300_deadwork_thread or vub300_cmndwork_thread
	 */
	int retval;
	retval =
		usb_control_msg(vub300->udev, usb_rcvctrlpipe(vub300->udev, 0),
				GET_SYSTEM_PORT_STATUS,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x0000, 0x0000, &vub300->system_port_status,
				sizeof(vub300->system_port_status), HZ);
	if (sizeof(vub300->system_port_status) == retval)
		new_system_port_status(vub300);
}

static void __vub300_irqpoll_response(struct vub300_mmc_host *vub300)
{
	/* cmd_mutex is held by vub300_pollwork_thread */
	if (vub300->command_res_urb->actual_length == 0)
		return;

	switch (vub300->resp.common.header_type) {
	case RESPONSE_INTERRUPT:
		mutex_lock(&vub300->irq_mutex);
		if (vub300->irq_enabled)
			mmc_signal_sdio_irq(vub300->mmc);
		else
			vub300->irqs_queued += 1;
		vub300->irq_disabled = 1;
		mutex_unlock(&vub300->irq_mutex);
		break;
	case RESPONSE_ERROR:
		if (vub300->resp.error.error_code == SD_ERROR_NO_DEVICE)
			check_vub300_port_status(vub300);
		break;
	case RESPONSE_STATUS:
		vub300->system_port_status = vub300->resp.status;
		new_system_port_status(vub300);
		if (!vub300->card_present)
			vub300_queue_poll_work(vub300, HZ / 5);
		break;
	case RESPONSE_IRQ_DISABLED:
	{
		int offloaded_data_length = vub300->resp.common.header_size - 3;
		int register_count = offloaded_data_length >> 3;
		int ri = 0;
		while (register_count--) {
			add_offloaded_reg(vub300, &vub300->resp.irq.reg[ri]);
			ri += 1;
		}
		mutex_lock(&vub300->irq_mutex);
		if (vub300->irq_enabled)
			mmc_signal_sdio_irq(vub300->mmc);
		else
			vub300->irqs_queued += 1;
		vub300->irq_disabled = 1;
		mutex_unlock(&vub300->irq_mutex);
		break;
	}
	case RESPONSE_IRQ_ENABLED:
	{
		int offloaded_data_length = vub300->resp.common.header_size - 3;
		int register_count = offloaded_data_length >> 3;
		int ri = 0;
		while (register_count--) {
			add_offloaded_reg(vub300, &vub300->resp.irq.reg[ri]);
			ri += 1;
		}
		mutex_lock(&vub300->irq_mutex);
		if (vub300->irq_enabled)
			mmc_signal_sdio_irq(vub300->mmc);
		else if (vub300->irqs_queued)
			vub300->irqs_queued += 1;
		else
			vub300->irqs_queued += 1;
		vub300->irq_disabled = 0;
		mutex_unlock(&vub300->irq_mutex);
		break;
	}
	case RESPONSE_NO_INTERRUPT:
		vub300_queue_poll_work(vub300, 1);
		break;
	default:
		break;
	}
}

static void __do_poll(struct vub300_mmc_host *vub300)
{
	/* cmd_mutex is held by vub300_pollwork_thread */
	long commretval;
	mod_timer(&vub300->inactivity_timer, jiffies + HZ);
	init_completion(&vub300->irqpoll_complete);
	send_irqpoll(vub300);
	commretval = wait_for_completion_timeout(&vub300->irqpoll_complete,
						 msecs_to_jiffies(500));
	if (vub300->usb_transport_fail) {
		/* no need to do anything */
	} else if (commretval == 0) {
		vub300->usb_timed_out = 1;
		usb_kill_urb(vub300->command_out_urb);
		usb_kill_urb(vub300->command_res_urb);
	} else if (commretval < 0) {
		vub300_queue_poll_work(vub300, 1);
	} else { /* commretval > 0 */
		__vub300_irqpoll_response(vub300);
	}
}

/* this thread runs only when the driver
 * is trying to poll the device for an IRQ
 */
static void vub300_pollwork_thread(struct work_struct *work)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 = container_of(work,
			      struct vub300_mmc_host, pollwork.work);
	if (!vub300->interface) {
		kref_put(&vub300->kref, vub300_delete);
		return;
	}
	mutex_lock(&vub300->cmd_mutex);
	if (vub300->cmd) {
		vub300_queue_poll_work(vub300, 1);
	} else if (!vub300->card_present) {
		/* no need to do anything */
	} else { /* vub300->card_present */
		mutex_lock(&vub300->irq_mutex);
		if (!vub300->irq_enabled) {
			mutex_unlock(&vub300->irq_mutex);
		} else if (vub300->irqs_queued) {
			vub300->irqs_queued -= 1;
			mmc_signal_sdio_irq(vub300->mmc);
			mod_timer(&vub300->inactivity_timer, jiffies + HZ);
			mutex_unlock(&vub300->irq_mutex);
		} else { /* NOT vub300->irqs_queued */
			mutex_unlock(&vub300->irq_mutex);
			__do_poll(vub300);
		}
	}
	mutex_unlock(&vub300->cmd_mutex);
	kref_put(&vub300->kref, vub300_delete);
}

static void vub300_deadwork_thread(struct work_struct *work)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 =
		container_of(work, struct vub300_mmc_host, deadwork);
	if (!vub300->interface) {
		kref_put(&vub300->kref, vub300_delete);
		return;
	}
	mutex_lock(&vub300->cmd_mutex);
	if (vub300->cmd) {
		/*
		 * a command got in as the inactivity
		 * timer expired - so we just let the
		 * processing of the command show if
		 * the device is dead
		 */
	} else if (vub300->card_present) {
		check_vub300_port_status(vub300);
	} else if (vub300->mmc && vub300->mmc->card &&
		   mmc_card_present(vub300->mmc->card)) {
		/*
		 * the MMC core must not have responded
		 * to the previous indication - lets
		 * hope that it eventually does so we
		 * will just ignore this for now
		 */
	} else {
		check_vub300_port_status(vub300);
	}
	mod_timer(&vub300->inactivity_timer, jiffies + HZ);
	mutex_unlock(&vub300->cmd_mutex);
	kref_put(&vub300->kref, vub300_delete);
}

static void vub300_inactivity_timer_expired(unsigned long data)
{				/* softirq */
	struct vub300_mmc_host *vub300 = (struct vub300_mmc_host *)data;
	if (!vub300->interface) {
		kref_put(&vub300->kref, vub300_delete);
	} else if (vub300->cmd) {
		mod_timer(&vub300->inactivity_timer, jiffies + HZ);
	} else {
		vub300_queue_dead_work(vub300);
		mod_timer(&vub300->inactivity_timer, jiffies + HZ);
	}
}

static int vub300_response_error(u8 error_code)
{
	switch (error_code) {
	case SD_ERROR_PIO_TIMEOUT:
	case SD_ERROR_1BIT_TIMEOUT:
	case SD_ERROR_4BIT_TIMEOUT:
		return -ETIMEDOUT;
	case SD_ERROR_STAT_DATA:
	case SD_ERROR_OVERRUN:
	case SD_ERROR_STAT_CMD:
	case SD_ERROR_STAT_CMD_TIMEOUT:
	case SD_ERROR_SDCRDY_STUCK:
	case SD_ERROR_UNHANDLED:
	case SD_ERROR_1BIT_CRC_WRONG:
	case SD_ERROR_4BIT_CRC_WRONG:
	case SD_ERROR_1BIT_CRC_ERROR:
	case SD_ERROR_4BIT_CRC_ERROR:
	case SD_ERROR_NO_CMD_ENDBIT:
	case SD_ERROR_NO_1BIT_DATEND:
	case SD_ERROR_NO_4BIT_DATEND:
	case SD_ERROR_1BIT_DATA_TIMEOUT:
	case SD_ERROR_4BIT_DATA_TIMEOUT:
	case SD_ERROR_1BIT_UNEXPECTED_TIMEOUT:
	case SD_ERROR_4BIT_UNEXPECTED_TIMEOUT:
		return -EILSEQ;
	case 33:
		return -EILSEQ;
	case SD_ERROR_ILLEGAL_COMMAND:
		return -EINVAL;
	case SD_ERROR_NO_DEVICE:
		return -ENOMEDIUM;
	default:
		return -ENODEV;
	}
}

static void command_res_completed(struct urb *urb)
{				/* urb completion handler - hardirq */
	struct vub300_mmc_host *vub300 = (struct vub300_mmc_host *)urb->context;
	if (urb->status) {
		/* we have to let the initiator handle the error */
	} else if (vub300->command_res_urb->actual_length == 0) {
		/*
		 * we have seen this happen once or twice and
		 * we suspect a buggy USB host controller
		 */
	} else if (!vub300->data) {
		/* this means that the command (typically CMD52) suceeded */
	} else if (vub300->resp.common.header_type != 0x02) {
		/*
		 * this is an error response from the VUB300 chip
		 * and we let the initiator handle it
		 */
	} else if (vub300->urb) {
		vub300->cmd->error =
			vub300_response_error(vub300->resp.error.error_code);
		usb_unlink_urb(vub300->urb);
	} else {
		vub300->cmd->error =
			vub300_response_error(vub300->resp.error.error_code);
		usb_sg_cancel(&vub300->sg_request);
	}
	complete(&vub300->command_complete);	/* got_response_in */
}

static void command_out_completed(struct urb *urb)
{				/* urb completion handler - hardirq */
	struct vub300_mmc_host *vub300 = (struct vub300_mmc_host *)urb->context;
	if (urb->status) {
		complete(&vub300->command_complete);
	} else {
		int ret;
		unsigned int pipe =
			usb_rcvbulkpipe(vub300->udev, vub300->cmnd_res_ep);
		usb_fill_bulk_urb(vub300->command_res_urb, vub300->udev, pipe,
				  &vub300->resp, sizeof(vub300->resp),
				  command_res_completed, vub300);
		vub300->command_res_urb->actual_length = 0;
		ret = usb_submit_urb(vub300->command_res_urb, GFP_ATOMIC);
		if (ret == 0) {
			/*
			 * the urb completion handler will call
			 * our completion handler
			 */
		} else {
			/*
			 * and thus we only call it directly
			 * when it will not be called
			 */
			complete(&vub300->command_complete);
		}
	}
}

/*
 * the STUFF bits are masked out for the comparisons
 */
static void snoop_block_size_and_bus_width(struct vub300_mmc_host *vub300,
					   u32 cmd_arg)
{
	if ((0xFBFFFE00 & cmd_arg) == 0x80022200)
		vub300->fbs[1] = (cmd_arg << 8) | (0x00FF & vub300->fbs[1]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x80022000)
		vub300->fbs[1] = (0xFF & cmd_arg) | (0xFF00 & vub300->fbs[1]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x80042200)
		vub300->fbs[2] = (cmd_arg << 8) | (0x00FF & vub300->fbs[2]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x80042000)
		vub300->fbs[2] = (0xFF & cmd_arg) | (0xFF00 & vub300->fbs[2]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x80062200)
		vub300->fbs[3] = (cmd_arg << 8) | (0x00FF & vub300->fbs[3]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x80062000)
		vub300->fbs[3] = (0xFF & cmd_arg) | (0xFF00 & vub300->fbs[3]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x80082200)
		vub300->fbs[4] = (cmd_arg << 8) | (0x00FF & vub300->fbs[4]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x80082000)
		vub300->fbs[4] = (0xFF & cmd_arg) | (0xFF00 & vub300->fbs[4]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x800A2200)
		vub300->fbs[5] = (cmd_arg << 8) | (0x00FF & vub300->fbs[5]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x800A2000)
		vub300->fbs[5] = (0xFF & cmd_arg) | (0xFF00 & vub300->fbs[5]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x800C2200)
		vub300->fbs[6] = (cmd_arg << 8) | (0x00FF & vub300->fbs[6]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x800C2000)
		vub300->fbs[6] = (0xFF & cmd_arg) | (0xFF00 & vub300->fbs[6]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x800E2200)
		vub300->fbs[7] = (cmd_arg << 8) | (0x00FF & vub300->fbs[7]);
	else if ((0xFBFFFE00 & cmd_arg) == 0x800E2000)
		vub300->fbs[7] = (0xFF & cmd_arg) | (0xFF00 & vub300->fbs[7]);
	else if ((0xFBFFFE03 & cmd_arg) == 0x80000E00)
		vub300->bus_width = 1;
	else if ((0xFBFFFE03 & cmd_arg) == 0x80000E02)
		vub300->bus_width = 4;
}

static void send_command(struct vub300_mmc_host *vub300)
{
	/* cmd_mutex is held by vub300_cmndwork_thread */
	struct mmc_command *cmd = vub300->cmd;
	struct mmc_data *data = vub300->data;
	int retval;
	int i;
	u8 response_type;
	if (vub300->app_spec) {
		switch (cmd->opcode) {
		case 6:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			if (0x00000000 == (0x00000003 & cmd->arg))
				vub300->bus_width = 1;
			else if (0x00000002 == (0x00000003 & cmd->arg))
				vub300->bus_width = 4;
			else
				dev_err(&vub300->udev->dev,
					"unexpected ACMD6 bus_width=%d\n",
					0x00000003 & cmd->arg);
			break;
		case 13:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 22:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 23:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 41:
			response_type = SDRT_3;
			vub300->resp_len = 6;
			break;
		case 42:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 51:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 55:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		default:
			vub300->resp_len = 0;
			cmd->error = -EINVAL;
			complete(&vub300->command_complete);
			return;
		}
		vub300->app_spec = 0;
	} else {
		switch (cmd->opcode) {
		case 0:
			response_type = SDRT_NONE;
			vub300->resp_len = 0;
			break;
		case 1:
			response_type = SDRT_3;
			vub300->resp_len = 6;
			break;
		case 2:
			response_type = SDRT_2;
			vub300->resp_len = 17;
			break;
		case 3:
			response_type = SDRT_6;
			vub300->resp_len = 6;
			break;
		case 4:
			response_type = SDRT_NONE;
			vub300->resp_len = 0;
			break;
		case 5:
			response_type = SDRT_4;
			vub300->resp_len = 6;
			break;
		case 6:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 7:
			response_type = SDRT_1B;
			vub300->resp_len = 6;
			break;
		case 8:
			response_type = SDRT_7;
			vub300->resp_len = 6;
			break;
		case 9:
			response_type = SDRT_2;
			vub300->resp_len = 17;
			break;
		case 10:
			response_type = SDRT_2;
			vub300->resp_len = 17;
			break;
		case 12:
			response_type = SDRT_1B;
			vub300->resp_len = 6;
			break;
		case 13:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 15:
			response_type = SDRT_NONE;
			vub300->resp_len = 0;
			break;
		case 16:
			for (i = 0; i < ARRAY_SIZE(vub300->fbs); i++)
				vub300->fbs[i] = 0xFFFF & cmd->arg;
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 17:
		case 18:
		case 24:
		case 25:
		case 27:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 28:
		case 29:
			response_type = SDRT_1B;
			vub300->resp_len = 6;
			break;
		case 30:
		case 32:
		case 33:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 38:
			response_type = SDRT_1B;
			vub300->resp_len = 6;
			break;
		case 42:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		case 52:
			response_type = SDRT_5;
			vub300->resp_len = 6;
			snoop_block_size_and_bus_width(vub300, cmd->arg);
			break;
		case 53:
			response_type = SDRT_5;
			vub300->resp_len = 6;
			break;
		case 55:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			vub300->app_spec = 1;
			break;
		case 56:
			response_type = SDRT_1;
			vub300->resp_len = 6;
			break;
		default:
			vub300->resp_len = 0;
			cmd->error = -EINVAL;
			complete(&vub300->command_complete);
			return;
		}
	}
	/*
	 * it is a shame that we can not use "sizeof(struct sd_command_header)"
	 * this is because the packet _must_ be padded to 64 bytes
	 */
	vub300->cmnd.head.header_size = 20;
	vub300->cmnd.head.header_type = 0x00;
	vub300->cmnd.head.port_number = 0; /* "0" means port 1 */
	vub300->cmnd.head.command_type = 0x00; /* standard read command */
	vub300->cmnd.head.response_type = response_type;
	vub300->cmnd.head.command_index = cmd->opcode;
	vub300->cmnd.head.arguments[0] = cmd->arg >> 24;
	vub300->cmnd.head.arguments[1] = cmd->arg >> 16;
	vub300->cmnd.head.arguments[2] = cmd->arg >> 8;
	vub300->cmnd.head.arguments[3] = cmd->arg >> 0;
	if (cmd->opcode == 52) {
		int fn = 0x7 & (cmd->arg >> 28);
		vub300->cmnd.head.block_count[0] = 0;
		vub300->cmnd.head.block_count[1] = 0;
		vub300->cmnd.head.block_size[0] = (vub300->fbs[fn] >> 8) & 0xFF;
		vub300->cmnd.head.block_size[1] = (vub300->fbs[fn] >> 0) & 0xFF;
		vub300->cmnd.head.command_type = 0x00;
		vub300->cmnd.head.transfer_size[0] = 0;
		vub300->cmnd.head.transfer_size[1] = 0;
		vub300->cmnd.head.transfer_size[2] = 0;
		vub300->cmnd.head.transfer_size[3] = 0;
	} else if (!data) {
		vub300->cmnd.head.block_count[0] = 0;
		vub300->cmnd.head.block_count[1] = 0;
		vub300->cmnd.head.block_size[0] = (vub300->fbs[0] >> 8) & 0xFF;
		vub300->cmnd.head.block_size[1] = (vub300->fbs[0] >> 0) & 0xFF;
		vub300->cmnd.head.command_type = 0x00;
		vub300->cmnd.head.transfer_size[0] = 0;
		vub300->cmnd.head.transfer_size[1] = 0;
		vub300->cmnd.head.transfer_size[2] = 0;
		vub300->cmnd.head.transfer_size[3] = 0;
	} else if (cmd->opcode == 53) {
		int fn = 0x7 & (cmd->arg >> 28);
		if (0x08 & vub300->cmnd.head.arguments[0]) { /* BLOCK MODE */
			vub300->cmnd.head.block_count[0] =
				(data->blocks >> 8) & 0xFF;
			vub300->cmnd.head.block_count[1] =
				(data->blocks >> 0) & 0xFF;
			vub300->cmnd.head.block_size[0] =
				(data->blksz >> 8) & 0xFF;
			vub300->cmnd.head.block_size[1] =
				(data->blksz >> 0) & 0xFF;
		} else {	/* BYTE MODE */
			vub300->cmnd.head.block_count[0] = 0;
			vub300->cmnd.head.block_count[1] = 0;
			vub300->cmnd.head.block_size[0] =
				(vub300->datasize >> 8) & 0xFF;
			vub300->cmnd.head.block_size[1] =
				(vub300->datasize >> 0) & 0xFF;
		}
		vub300->cmnd.head.command_type =
			(MMC_DATA_READ & data->flags) ? 0x00 : 0x80;
		vub300->cmnd.head.transfer_size[0] =
			(vub300->datasize >> 24) & 0xFF;
		vub300->cmnd.head.transfer_size[1] =
			(vub300->datasize >> 16) & 0xFF;
		vub300->cmnd.head.transfer_size[2] =
			(vub300->datasize >> 8) & 0xFF;
		vub300->cmnd.head.transfer_size[3] =
			(vub300->datasize >> 0) & 0xFF;
		if (vub300->datasize < vub300->fbs[fn]) {
			vub300->cmnd.head.block_count[0] = 0;
			vub300->cmnd.head.block_count[1] = 0;
		}
	} else {
		vub300->cmnd.head.block_count[0] = (data->blocks >> 8) & 0xFF;
		vub300->cmnd.head.block_count[1] = (data->blocks >> 0) & 0xFF;
		vub300->cmnd.head.block_size[0] = (data->blksz >> 8) & 0xFF;
		vub300->cmnd.head.block_size[1] = (data->blksz >> 0) & 0xFF;
		vub300->cmnd.head.command_type =
			(MMC_DATA_READ & data->flags) ? 0x00 : 0x80;
		vub300->cmnd.head.transfer_size[0] =
			(vub300->datasize >> 24) & 0xFF;
		vub300->cmnd.head.transfer_size[1] =
			(vub300->datasize >> 16) & 0xFF;
		vub300->cmnd.head.transfer_size[2] =
			(vub300->datasize >> 8) & 0xFF;
		vub300->cmnd.head.transfer_size[3] =
			(vub300->datasize >> 0) & 0xFF;
		if (vub300->datasize < vub300->fbs[0]) {
			vub300->cmnd.head.block_count[0] = 0;
			vub300->cmnd.head.block_count[1] = 0;
		}
	}
	if (vub300->cmnd.head.block_size[0] || vub300->cmnd.head.block_size[1]) {
		u16 block_size = vub300->cmnd.head.block_size[1] |
			(vub300->cmnd.head.block_size[0] << 8);
		u16 block_boundary = FIRMWARE_BLOCK_BOUNDARY -
			(FIRMWARE_BLOCK_BOUNDARY % block_size);
		vub300->cmnd.head.block_boundary[0] =
			(block_boundary >> 8) & 0xFF;
		vub300->cmnd.head.block_boundary[1] =
			(block_boundary >> 0) & 0xFF;
	} else {
		vub300->cmnd.head.block_boundary[0] = 0;
		vub300->cmnd.head.block_boundary[1] = 0;
	}
	usb_fill_bulk_urb(vub300->command_out_urb, vub300->udev,
			  usb_sndbulkpipe(vub300->udev, vub300->cmnd_out_ep),
			  &vub300->cmnd, sizeof(vub300->cmnd),
			  command_out_completed, vub300);
	retval = usb_submit_urb(vub300->command_out_urb, GFP_KERNEL);
	if (retval < 0) {
		cmd->error = retval;
		complete(&vub300->command_complete);
		return;
	} else {
		return;
	}
}

/*
 * timer callback runs in atomic mode
 *       so it cannot call usb_kill_urb()
 */
static void vub300_sg_timed_out(unsigned long data)
{
	struct vub300_mmc_host *vub300 = (struct vub300_mmc_host *)data;
	vub300->usb_timed_out = 1;
	usb_sg_cancel(&vub300->sg_request);
	usb_unlink_urb(vub300->command_out_urb);
	usb_unlink_urb(vub300->command_res_urb);
}

static u16 roundup_to_multiple_of_64(u16 number)
{
	return 0xFFC0 & (0x3F + number);
}

/*
 * this is a separate function to solve the 80 column width restriction
 */
static void __download_offload_pseudocode(struct vub300_mmc_host *vub300,
					  const struct firmware *fw)
{
	u8 register_count = 0;
	u16 ts = 0;
	u16 interrupt_size = 0;
	const u8 *data = fw->data;
	int size = fw->size;
	u8 c;
	dev_info(&vub300->udev->dev, "using %s for SDIO offload processing\n",
		 vub300->vub_name);
	do {
		c = *data++;
	} while (size-- && c); /* skip comment */
	dev_info(&vub300->udev->dev, "using offload firmware %s %s\n", fw->data,
		 vub300->vub_name);
	if (size < 4) {
		dev_err(&vub300->udev->dev,
			"corrupt offload pseudocode in firmware %s\n",
			vub300->vub_name);
		strncpy(vub300->vub_name, "corrupt offload pseudocode",
			sizeof(vub300->vub_name));
		return;
	}
	interrupt_size += *data++;
	size -= 1;
	interrupt_size <<= 8;
	interrupt_size += *data++;
	size -= 1;
	if (interrupt_size < size) {
		u16 xfer_length = roundup_to_multiple_of_64(interrupt_size);
		u8 *xfer_buffer = kmalloc(xfer_length, GFP_KERNEL);
		if (xfer_buffer) {
			int retval;
			memcpy(xfer_buffer, data, interrupt_size);
			memset(xfer_buffer + interrupt_size, 0,
			       xfer_length - interrupt_size);
			size -= interrupt_size;
			data += interrupt_size;
			retval =
				usb_control_msg(vub300->udev,
						usb_sndctrlpipe(vub300->udev, 0),
						SET_INTERRUPT_PSEUDOCODE,
						USB_DIR_OUT | USB_TYPE_VENDOR |
						USB_RECIP_DEVICE, 0x0000, 0x0000,
						xfer_buffer, xfer_length, HZ);
			kfree(xfer_buffer);
			if (retval < 0) {
				strncpy(vub300->vub_name,
					"SDIO pseudocode download failed",
					sizeof(vub300->vub_name));
				return;
			}
		} else {
			dev_err(&vub300->udev->dev,
				"not enough memory for xfer buffer to send"
				" INTERRUPT_PSEUDOCODE for %s %s\n", fw->data,
				vub300->vub_name);
			strncpy(vub300->vub_name,
				"SDIO interrupt pseudocode download failed",
				sizeof(vub300->vub_name));
			return;
		}
	} else {
		dev_err(&vub300->udev->dev,
			"corrupt interrupt pseudocode in firmware %s %s\n",
			fw->data, vub300->vub_name);
		strncpy(vub300->vub_name, "corrupt interrupt pseudocode",
			sizeof(vub300->vub_name));
		return;
	}
	ts += *data++;
	size -= 1;
	ts <<= 8;
	ts += *data++;
	size -= 1;
	if (ts < size) {
		u16 xfer_length = roundup_to_multiple_of_64(ts);
		u8 *xfer_buffer = kmalloc(xfer_length, GFP_KERNEL);
		if (xfer_buffer) {
			int retval;
			memcpy(xfer_buffer, data, ts);
			memset(xfer_buffer + ts, 0,
			       xfer_length - ts);
			size -= ts;
			data += ts;
			retval =
				usb_control_msg(vub300->udev,
						usb_sndctrlpipe(vub300->udev, 0),
						SET_TRANSFER_PSEUDOCODE,
						USB_DIR_OUT | USB_TYPE_VENDOR |
						USB_RECIP_DEVICE, 0x0000, 0x0000,
						xfer_buffer, xfer_length, HZ);
			kfree(xfer_buffer);
			if (retval < 0) {
				strncpy(vub300->vub_name,
					"SDIO pseudocode download failed",
					sizeof(vub300->vub_name));
				return;
			}
		} else {
			dev_err(&vub300->udev->dev,
				"not enough memory for xfer buffer to send"
				" TRANSFER_PSEUDOCODE for %s %s\n", fw->data,
				vub300->vub_name);
			strncpy(vub300->vub_name,
				"SDIO transfer pseudocode download failed",
				sizeof(vub300->vub_name));
			return;
		}
	} else {
		dev_err(&vub300->udev->dev,
			"corrupt transfer pseudocode in firmware %s %s\n",
			fw->data, vub300->vub_name);
		strncpy(vub300->vub_name, "corrupt transfer pseudocode",
			sizeof(vub300->vub_name));
		return;
	}
	register_count += *data++;
	size -= 1;
	if (register_count * 4 == size) {
		int I = vub300->dynamic_register_count = register_count;
		int i = 0;
		while (I--) {
			unsigned int func_num = 0;
			vub300->sdio_register[i].func_num = *data++;
			size -= 1;
			func_num += *data++;
			size -= 1;
			func_num <<= 8;
			func_num += *data++;
			size -= 1;
			func_num <<= 8;
			func_num += *data++;
			size -= 1;
			vub300->sdio_register[i].sdio_reg = func_num;
			vub300->sdio_register[i].activate = 1;
			vub300->sdio_register[i].prepared = 0;
			i += 1;
		}
		dev_info(&vub300->udev->dev,
			 "initialized %d dynamic pseudocode registers\n",
			 vub300->dynamic_register_count);
		return;
	} else {
		dev_err(&vub300->udev->dev,
			"corrupt dynamic registers in firmware %s\n",
			vub300->vub_name);
		strncpy(vub300->vub_name, "corrupt dynamic registers",
			sizeof(vub300->vub_name));
		return;
	}
}

/*
 * if the binary containing the EMPTY PseudoCode can not be found
 * vub300->vub_name is set anyway in order to prevent an automatic retry
 */
static void download_offload_pseudocode(struct vub300_mmc_host *vub300)
{
	struct mmc_card *card = vub300->mmc->card;
	int sdio_funcs = card->sdio_funcs;
	const struct firmware *fw = NULL;
	int l = snprintf(vub300->vub_name, sizeof(vub300->vub_name),
			 "vub_%04X%04X", card->cis.vendor, card->cis.device);
	int n = 0;
	int retval;
	for (n = 0; n < sdio_funcs; n++) {
		struct sdio_func *sf = card->sdio_func[n];
		l += snprintf(vub300->vub_name + l,
			      sizeof(vub300->vub_name) - l, "_%04X%04X",
			      sf->vendor, sf->device);
	};
	snprintf(vub300->vub_name + l, sizeof(vub300->vub_name) - l, ".bin");
	dev_info(&vub300->udev->dev, "requesting offload firmware %s\n",
		 vub300->vub_name);
	retval = request_firmware(&fw, vub300->vub_name, &card->dev);
	if (retval < 0) {
		strncpy(vub300->vub_name, "vub_default.bin",
			sizeof(vub300->vub_name));
		retval = request_firmware(&fw, vub300->vub_name, &card->dev);
		if (retval < 0) {
			strncpy(vub300->vub_name,
				"no SDIO offload firmware found",
				sizeof(vub300->vub_name));
		} else {
			__download_offload_pseudocode(vub300, fw);
			release_firmware(fw);
		}
	} else {
		__download_offload_pseudocode(vub300, fw);
		release_firmware(fw);
	}
}

static void vub300_usb_bulk_msg_completion(struct urb *urb)
{				/* urb completion handler - hardirq */
	complete((struct completion *)urb->context);
}

static int vub300_usb_bulk_msg(struct vub300_mmc_host *vub300,
			       unsigned int pipe, void *data, int len,
			       int *actual_length, int timeout_msecs)
{
	/* cmd_mutex is held by vub300_cmndwork_thread */
	struct usb_device *usb_dev = vub300->udev;
	struct completion done;
	int retval;
	vub300->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!vub300->urb)
		return -ENOMEM;
	usb_fill_bulk_urb(vub300->urb, usb_dev, pipe, data, len,
			  vub300_usb_bulk_msg_completion, NULL);
	init_completion(&done);
	vub300->urb->context = &done;
	vub300->urb->actual_length = 0;
	retval = usb_submit_urb(vub300->urb, GFP_KERNEL);
	if (unlikely(retval))
		goto out;
	if (!wait_for_completion_timeout
	    (&done, msecs_to_jiffies(timeout_msecs))) {
		retval = -ETIMEDOUT;
		usb_kill_urb(vub300->urb);
	} else {
		retval = vub300->urb->status;
	}
out:
	*actual_length = vub300->urb->actual_length;
	usb_free_urb(vub300->urb);
	vub300->urb = NULL;
	return retval;
}

static int __command_read_data(struct vub300_mmc_host *vub300,
			       struct mmc_command *cmd, struct mmc_data *data)
{
	/* cmd_mutex is held by vub300_cmndwork_thread */
	int linear_length = vub300->datasize;
	int padded_length = vub300->large_usb_packets ?
		((511 + linear_length) >> 9) << 9 :
		((63 + linear_length) >> 6) << 6;
	if ((padded_length == linear_length) || !pad_input_to_usb_pkt) {
		int result;
		unsigned pipe;
		pipe = usb_rcvbulkpipe(vub300->udev, vub300->data_inp_ep);
		result = usb_sg_init(&vub300->sg_request, vub300->udev,
				     pipe, 0, data->sg,
				     data->sg_len, 0, GFP_KERNEL);
		if (result < 0) {
			usb_unlink_urb(vub300->command_out_urb);
			usb_unlink_urb(vub300->command_res_urb);
			cmd->error = result;
			data->bytes_xfered = 0;
			return 0;
		} else {
			vub300->sg_transfer_timer.expires =
				jiffies + msecs_to_jiffies(2000 +
						  (linear_length / 16384));
			add_timer(&vub300->sg_transfer_timer);
			usb_sg_wait(&vub300->sg_request);
			del_timer(&vub300->sg_transfer_timer);
			if (vub300->sg_request.status < 0) {
				cmd->error = vub300->sg_request.status;
				data->bytes_xfered = 0;
				return 0;
			} else {
				data->bytes_xfered = vub300->datasize;
				return linear_length;
			}
		}
	} else {
		u8 *buf = kmalloc(padded_length, GFP_KERNEL);
		if (buf) {
			int result;
			unsigned pipe = usb_rcvbulkpipe(vub300->udev,
							vub300->data_inp_ep);
			int actual_length = 0;
			result = vub300_usb_bulk_msg(vub300, pipe, buf,
					     padded_length, &actual_length,
					     2000 + (padded_length / 16384));
			if (result < 0) {
				cmd->error = result;
				data->bytes_xfered = 0;
				kfree(buf);
				return 0;
			} else if (actual_length < linear_length) {
				cmd->error = -EREMOTEIO;
				data->bytes_xfered = 0;
				kfree(buf);
				return 0;
			} else {
				sg_copy_from_buffer(data->sg, data->sg_len, buf,
						    linear_length);
				kfree(buf);
				data->bytes_xfered = vub300->datasize;
				return linear_length;
			}
		} else {
			cmd->error = -ENOMEM;
			data->bytes_xfered = 0;
			return 0;
		}
	}
}

static int __command_write_data(struct vub300_mmc_host *vub300,
				struct mmc_command *cmd, struct mmc_data *data)
{
	/* cmd_mutex is held by vub300_cmndwork_thread */
	unsigned pipe = usb_sndbulkpipe(vub300->udev, vub300->data_out_ep);
	int linear_length = vub300->datasize;
	int modulo_64_length = linear_length & 0x003F;
	int modulo_512_length = linear_length & 0x01FF;
	if (linear_length < 64) {
		int result;
		int actual_length;
		sg_copy_to_buffer(data->sg, data->sg_len,
				  vub300->padded_buffer,
				  sizeof(vub300->padded_buffer));
		memset(vub300->padded_buffer + linear_length, 0,
		       sizeof(vub300->padded_buffer) - linear_length);
		result = vub300_usb_bulk_msg(vub300, pipe, vub300->padded_buffer,
					     sizeof(vub300->padded_buffer),
					     &actual_length, 2000 +
					     (sizeof(vub300->padded_buffer) /
					      16384));
		if (result < 0) {
			cmd->error = result;
			data->bytes_xfered = 0;
		} else {
			data->bytes_xfered = vub300->datasize;
		}
	} else if ((!vub300->large_usb_packets && (0 < modulo_64_length)) ||
		    (vub300->large_usb_packets && (64 > modulo_512_length))
		) {		/* don't you just love these work-rounds */
		int padded_length = ((63 + linear_length) >> 6) << 6;
		u8 *buf = kmalloc(padded_length, GFP_KERNEL);
		if (buf) {
			int result;
			int actual_length;
			sg_copy_to_buffer(data->sg, data->sg_len, buf,
					  padded_length);
			memset(buf + linear_length, 0,
			       padded_length - linear_length);
			result =
				vub300_usb_bulk_msg(vub300, pipe, buf,
						    padded_length, &actual_length,
						    2000 + padded_length / 16384);
			kfree(buf);
			if (result < 0) {
				cmd->error = result;
				data->bytes_xfered = 0;
			} else {
				data->bytes_xfered = vub300->datasize;
			}
		} else {
			cmd->error = -ENOMEM;
			data->bytes_xfered = 0;
		}
	} else {		/* no data padding required */
		int result;
		unsigned char buf[64 * 4];
		sg_copy_to_buffer(data->sg, data->sg_len, buf, sizeof(buf));
		result = usb_sg_init(&vub300->sg_request, vub300->udev,
				     pipe, 0, data->sg,
				     data->sg_len, 0, GFP_KERNEL);
		if (result < 0) {
			usb_unlink_urb(vub300->command_out_urb);
			usb_unlink_urb(vub300->command_res_urb);
			cmd->error = result;
			data->bytes_xfered = 0;
		} else {
			vub300->sg_transfer_timer.expires =
				jiffies + msecs_to_jiffies(2000 +
							   linear_length / 16384);
			add_timer(&vub300->sg_transfer_timer);
			usb_sg_wait(&vub300->sg_request);
			if (cmd->error) {
				data->bytes_xfered = 0;
			} else {
				del_timer(&vub300->sg_transfer_timer);
				if (vub300->sg_request.status < 0) {
					cmd->error = vub300->sg_request.status;
					data->bytes_xfered = 0;
				} else {
					data->bytes_xfered = vub300->datasize;
				}
			}
		}
	}
	return linear_length;
}

static void __vub300_command_response(struct vub300_mmc_host *vub300,
				      struct mmc_command *cmd,
				      struct mmc_data *data, int data_length)
{
	/* cmd_mutex is held by vub300_cmndwork_thread */
	long respretval;
	int msec_timeout = 1000 + data_length / 4;
	respretval =
		wait_for_completion_timeout(&vub300->command_complete,
					    msecs_to_jiffies(msec_timeout));
	if (respretval == 0) { /* TIMED OUT */
		/* we don't know which of "out" and "res" if any failed */
		int result;
		vub300->usb_timed_out = 1;
		usb_kill_urb(vub300->command_out_urb);
		usb_kill_urb(vub300->command_res_urb);
		cmd->error = -ETIMEDOUT;
		result = usb_lock_device_for_reset(vub300->udev,
						   vub300->interface);
		if (result == 0) {
			result = usb_reset_device(vub300->udev);
			usb_unlock_device(vub300->udev);
		}
	} else if (respretval < 0) {
		/* we don't know which of "out" and "res" if any failed */
		usb_kill_urb(vub300->command_out_urb);
		usb_kill_urb(vub300->command_res_urb);
		cmd->error = respretval;
	} else if (cmd->error) {
		/*
		 * the error occurred sending the command
		 * or receiving the response
		 */
	} else if (vub300->command_out_urb->status) {
		vub300->usb_transport_fail = vub300->command_out_urb->status;
		cmd->error = -EPROTO == vub300->command_out_urb->status ?
			-ESHUTDOWN : vub300->command_out_urb->status;
	} else if (vub300->command_res_urb->status) {
		vub300->usb_transport_fail = vub300->command_res_urb->status;
		cmd->error = -EPROTO == vub300->command_res_urb->status ?
			-ESHUTDOWN : vub300->command_res_urb->status;
	} else if (vub300->resp.common.header_type == 0x00) {
		/*
		 * the command completed successfully
		 * and there was no piggybacked data
		 */
	} else if (vub300->resp.common.header_type == RESPONSE_ERROR) {
		cmd->error =
			vub300_response_error(vub300->resp.error.error_code);
		if (vub300->data)
			usb_sg_cancel(&vub300->sg_request);
	} else if (vub300->resp.common.header_type == RESPONSE_PIGGYBACKED) {
		int offloaded_data_length =
			vub300->resp.common.header_size -
			sizeof(struct sd_register_header);
		int register_count = offloaded_data_length >> 3;
		int ri = 0;
		while (register_count--) {
			add_offloaded_reg(vub300, &vub300->resp.pig.reg[ri]);
			ri += 1;
		}
		vub300->resp.common.header_size =
			sizeof(struct sd_register_header);
		vub300->resp.common.header_type = 0x00;
		cmd->error = 0;
	} else if (vub300->resp.common.header_type == RESPONSE_PIG_DISABLED) {
		int offloaded_data_length =
			vub300->resp.common.header_size -
			sizeof(struct sd_register_header);
		int register_count = offloaded_data_length >> 3;
		int ri = 0;
		while (register_count--) {
			add_offloaded_reg(vub300, &vub300->resp.pig.reg[ri]);
			ri += 1;
		}
		mutex_lock(&vub300->irq_mutex);
		if (vub300->irqs_queued) {
			vub300->irqs_queued += 1;
		} else if (vub300->irq_enabled) {
			vub300->irqs_queued += 1;
			vub300_queue_poll_work(vub300, 0);
		} else {
			vub300->irqs_queued += 1;
		}
		vub300->irq_disabled = 1;
		mutex_unlock(&vub300->irq_mutex);
		vub300->resp.common.header_size =
			sizeof(struct sd_register_header);
		vub300->resp.common.header_type = 0x00;
		cmd->error = 0;
	} else if (vub300->resp.common.header_type == RESPONSE_PIG_ENABLED) {
		int offloaded_data_length =
			vub300->resp.common.header_size -
			sizeof(struct sd_register_header);
		int register_count = offloaded_data_length >> 3;
		int ri = 0;
		while (register_count--) {
			add_offloaded_reg(vub300, &vub300->resp.pig.reg[ri]);
			ri += 1;
		}
		mutex_lock(&vub300->irq_mutex);
		if (vub300->irqs_queued) {
			vub300->irqs_queued += 1;
		} else if (vub300->irq_enabled) {
			vub300->irqs_queued += 1;
			vub300_queue_poll_work(vub300, 0);
		} else {
			vub300->irqs_queued += 1;
		}
		vub300->irq_disabled = 0;
		mutex_unlock(&vub300->irq_mutex);
		vub300->resp.common.header_size =
			sizeof(struct sd_register_header);
		vub300->resp.common.header_type = 0x00;
		cmd->error = 0;
	} else {
		cmd->error = -EINVAL;
	}
}

static void construct_request_response(struct vub300_mmc_host *vub300,
				       struct mmc_command *cmd)
{
	int resp_len = vub300->resp_len;
	int less_cmd = (17 == resp_len) ? resp_len : resp_len - 1;
	int bytes = 3 & less_cmd;
	int words = less_cmd >> 2;
	u8 *r = vub300->resp.response.command_response;
	if (bytes == 3) {
		cmd->resp[words] = (r[1 + (words << 2)] << 24)
			| (r[2 + (words << 2)] << 16)
			| (r[3 + (words << 2)] << 8);
	} else if (bytes == 2) {
		cmd->resp[words] = (r[1 + (words << 2)] << 24)
			| (r[2 + (words << 2)] << 16);
	} else if (bytes == 1) {
		cmd->resp[words] = (r[1 + (words << 2)] << 24);
	}
	while (words-- > 0) {
		cmd->resp[words] = (r[1 + (words << 2)] << 24)
			| (r[2 + (words << 2)] << 16)
			| (r[3 + (words << 2)] << 8)
			| (r[4 + (words << 2)] << 0);
	}
	if ((cmd->opcode == 53) && (0x000000FF & cmd->resp[0]))
		cmd->resp[0] &= 0xFFFFFF00;
}

/* this thread runs only when there is an upper level command req outstanding */
static void vub300_cmndwork_thread(struct work_struct *work)
{
	struct vub300_mmc_host *vub300 =
		container_of(work, struct vub300_mmc_host, cmndwork);
	if (!vub300->interface) {
		kref_put(&vub300->kref, vub300_delete);
		return;
	} else {
		struct mmc_request *req = vub300->req;
		struct mmc_command *cmd = vub300->cmd;
		struct mmc_data *data = vub300->data;
		int data_length;
		mutex_lock(&vub300->cmd_mutex);
		init_completion(&vub300->command_complete);
		if (likely(vub300->vub_name[0]) || !vub300->mmc->card ||
		    !mmc_card_present(vub300->mmc->card)) {
			/*
			 * the name of the EMPTY Pseudo firmware file
			 * is used as a flag to indicate that the file
			 * has been already downloaded to the VUB300 chip
			 */
		} else if (0 == vub300->mmc->card->sdio_funcs) {
			strncpy(vub300->vub_name, "SD memory device",
				sizeof(vub300->vub_name));
		} else {
			download_offload_pseudocode(vub300);
		}
		send_command(vub300);
		if (!data)
			data_length = 0;
		else if (MMC_DATA_READ & data->flags)
			data_length = __command_read_data(vub300, cmd, data);
		else
			data_length = __command_write_data(vub300, cmd, data);
		__vub300_command_response(vub300, cmd, data, data_length);
		vub300->req = NULL;
		vub300->cmd = NULL;
		vub300->data = NULL;
		if (cmd->error) {
			if (cmd->error == -ENOMEDIUM)
				check_vub300_port_status(vub300);
			mutex_unlock(&vub300->cmd_mutex);
			mmc_request_done(vub300->mmc, req);
			kref_put(&vub300->kref, vub300_delete);
			return;
		} else {
			construct_request_response(vub300, cmd);
			vub300->resp_len = 0;
			mutex_unlock(&vub300->cmd_mutex);
			kref_put(&vub300->kref, vub300_delete);
			mmc_request_done(vub300->mmc, req);
			return;
		}
	}
}

static int examine_cyclic_buffer(struct vub300_mmc_host *vub300,
				 struct mmc_command *cmd, u8 Function)
{
	/* cmd_mutex is held by vub300_mmc_request */
	u8 cmd0 = 0xFF & (cmd->arg >> 24);
	u8 cmd1 = 0xFF & (cmd->arg >> 16);
	u8 cmd2 = 0xFF & (cmd->arg >> 8);
	u8 cmd3 = 0xFF & (cmd->arg >> 0);
	int first = MAXREGMASK & vub300->fn[Function].offload_point;
	struct offload_registers_access *rf = &vub300->fn[Function].reg[first];
	if (cmd0 == rf->command_byte[0] &&
	    cmd1 == rf->command_byte[1] &&
	    cmd2 == rf->command_byte[2] &&
	    cmd3 == rf->command_byte[3]) {
		u8 checksum = 0x00;
		cmd->resp[1] = checksum << 24;
		cmd->resp[0] = (rf->Respond_Byte[0] << 24)
			| (rf->Respond_Byte[1] << 16)
			| (rf->Respond_Byte[2] << 8)
			| (rf->Respond_Byte[3] << 0);
		vub300->fn[Function].offload_point += 1;
		vub300->fn[Function].offload_count -= 1;
		vub300->total_offload_count -= 1;
		return 1;
	} else {
		int delta = 1;	/* because it does not match the first one */
		u8 register_count = vub300->fn[Function].offload_count - 1;
		u32 register_point = vub300->fn[Function].offload_point + 1;
		while (0 < register_count) {
			int point = MAXREGMASK & register_point;
			struct offload_registers_access *r =
				&vub300->fn[Function].reg[point];
			if (cmd0 == r->command_byte[0] &&
			    cmd1 == r->command_byte[1] &&
			    cmd2 == r->command_byte[2] &&
			    cmd3 == r->command_byte[3]) {
				u8 checksum = 0x00;
				cmd->resp[1] = checksum << 24;
				cmd->resp[0] = (r->Respond_Byte[0] << 24)
					| (r->Respond_Byte[1] << 16)
					| (r->Respond_Byte[2] << 8)
					| (r->Respond_Byte[3] << 0);
				vub300->fn[Function].offload_point += delta;
				vub300->fn[Function].offload_count -= delta;
				vub300->total_offload_count -= delta;
				return 1;
			} else {
				register_point += 1;
				register_count -= 1;
				delta += 1;
				continue;
			}
		}
		return 0;
	}
}

static int satisfy_request_from_offloaded_data(struct vub300_mmc_host *vub300,
					       struct mmc_command *cmd)
{
	/* cmd_mutex is held by vub300_mmc_request */
	u8 regs = vub300->dynamic_register_count;
	u8 i = 0;
	u8 func = FUN(cmd);
	u32 reg = REG(cmd);
	while (0 < regs--) {
		if ((vub300->sdio_register[i].func_num == func) &&
		    (vub300->sdio_register[i].sdio_reg == reg)) {
			if (!vub300->sdio_register[i].prepared) {
				return 0;
			} else if ((0x80000000 & cmd->arg) == 0x80000000) {
				/*
				 * a write to a dynamic register
				 * nullifies our offloaded value
				 */
				vub300->sdio_register[i].prepared = 0;
				return 0;
			} else {
				u8 checksum = 0x00;
				u8 rsp0 = 0x00;
				u8 rsp1 = 0x00;
				u8 rsp2 = vub300->sdio_register[i].response;
				u8 rsp3 = vub300->sdio_register[i].regvalue;
				vub300->sdio_register[i].prepared = 0;
				cmd->resp[1] = checksum << 24;
				cmd->resp[0] = (rsp0 << 24)
					| (rsp1 << 16)
					| (rsp2 << 8)
					| (rsp3 << 0);
				return 1;
			}
		} else {
			i += 1;
			continue;
		}
	};
	if (vub300->total_offload_count == 0)
		return 0;
	else if (vub300->fn[func].offload_count == 0)
		return 0;
	else
		return examine_cyclic_buffer(vub300, cmd, func);
}

static void vub300_mmc_request(struct mmc_host *mmc, struct mmc_request *req)
{				/* NOT irq */
	struct mmc_command *cmd = req->cmd;
	struct vub300_mmc_host *vub300 = mmc_priv(mmc);
	if (!vub300->interface) {
		cmd->error = -ESHUTDOWN;
		mmc_request_done(mmc, req);
		return;
	} else {
		struct mmc_data *data = req->data;
		if (!vub300->card_powered) {
			cmd->error = -ENOMEDIUM;
			mmc_request_done(mmc, req);
			return;
		}
		if (!vub300->card_present) {
			cmd->error = -ENOMEDIUM;
			mmc_request_done(mmc, req);
			return;
		}
		if (vub300->usb_transport_fail) {
			cmd->error = vub300->usb_transport_fail;
			mmc_request_done(mmc, req);
			return;
		}
		if (!vub300->interface) {
			cmd->error = -ENODEV;
			mmc_request_done(mmc, req);
			return;
		}
		kref_get(&vub300->kref);
		mutex_lock(&vub300->cmd_mutex);
		mod_timer(&vub300->inactivity_timer, jiffies + HZ);
		/*
		 * for performance we have to return immediately
		 * if the requested data has been offloaded
		 */
		if (cmd->opcode == 52 &&
		    satisfy_request_from_offloaded_data(vub300, cmd)) {
			cmd->error = 0;
			mutex_unlock(&vub300->cmd_mutex);
			kref_put(&vub300->kref, vub300_delete);
			mmc_request_done(mmc, req);
			return;
		} else {
			vub300->cmd = cmd;
			vub300->req = req;
			vub300->data = data;
			if (data)
				vub300->datasize = data->blksz * data->blocks;
			else
				vub300->datasize = 0;
			vub300_queue_cmnd_work(vub300);
			mutex_unlock(&vub300->cmd_mutex);
			kref_put(&vub300->kref, vub300_delete);
			/*
			 * the kernel lock diagnostics complain
			 * if the cmd_mutex * is "passed on"
			 * to the cmndwork thread,
			 * so we must release it now
			 * and re-acquire it in the cmndwork thread
			 */
		}
	}
}

static void __set_clock_speed(struct vub300_mmc_host *vub300, u8 buf[8],
			      struct mmc_ios *ios)
{
	int buf_array_size = 8; /* ARRAY_SIZE(buf) does not work !!! */
	int retval;
	u32 kHzClock;
	if (ios->clock >= 48000000)
		kHzClock = 48000;
	else if (ios->clock >= 24000000)
		kHzClock = 24000;
	else if (ios->clock >= 20000000)
		kHzClock = 20000;
	else if (ios->clock >= 15000000)
		kHzClock = 15000;
	else if (ios->clock >= 200000)
		kHzClock = 200;
	else
		kHzClock = 0;
	{
		int i;
		u64 c = kHzClock;
		for (i = 0; i < buf_array_size; i++) {
			buf[i] = c;
			c >>= 8;
		}
	}
	retval =
		usb_control_msg(vub300->udev, usb_sndctrlpipe(vub300->udev, 0),
				SET_CLOCK_SPEED,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x00, 0x00, buf, buf_array_size, HZ);
	if (retval != 8) {
		dev_err(&vub300->udev->dev, "SET_CLOCK_SPEED"
			" %dkHz failed with retval=%d\n", kHzClock, retval);
	} else {
		dev_dbg(&vub300->udev->dev, "SET_CLOCK_SPEED"
			" %dkHz\n", kHzClock);
	}
}

static void vub300_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 = mmc_priv(mmc);
	if (!vub300->interface)
		return;
	kref_get(&vub300->kref);
	mutex_lock(&vub300->cmd_mutex);
	if ((ios->power_mode == MMC_POWER_OFF) && vub300->card_powered) {
		vub300->card_powered = 0;
		usb_control_msg(vub300->udev, usb_sndctrlpipe(vub300->udev, 0),
				SET_SD_POWER,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x0000, 0x0000, NULL, 0, HZ);
		/* must wait for the VUB300 u-proc to boot up */
		msleep(600);
	} else if ((ios->power_mode == MMC_POWER_UP) && !vub300->card_powered) {
		usb_control_msg(vub300->udev, usb_sndctrlpipe(vub300->udev, 0),
				SET_SD_POWER,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x0001, 0x0000, NULL, 0, HZ);
		msleep(600);
		vub300->card_powered = 1;
	} else if (ios->power_mode == MMC_POWER_ON) {
		u8 *buf = kmalloc(8, GFP_KERNEL);
		if (buf) {
			__set_clock_speed(vub300, buf, ios);
			kfree(buf);
		}
	} else {
		/* this should mean no change of state */
	}
	mutex_unlock(&vub300->cmd_mutex);
	kref_put(&vub300->kref, vub300_delete);
}

static int vub300_mmc_get_ro(struct mmc_host *mmc)
{
	struct vub300_mmc_host *vub300 = mmc_priv(mmc);
	return vub300->read_only;
}

static void vub300_enable_sdio_irq(struct mmc_host *mmc, int enable)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 = mmc_priv(mmc);
	if (!vub300->interface)
		return;
	kref_get(&vub300->kref);
	if (enable) {
		mutex_lock(&vub300->irq_mutex);
		if (vub300->irqs_queued) {
			vub300->irqs_queued -= 1;
			mmc_signal_sdio_irq(vub300->mmc);
		} else if (vub300->irq_disabled) {
			vub300->irq_disabled = 0;
			vub300->irq_enabled = 1;
			vub300_queue_poll_work(vub300, 0);
		} else if (vub300->irq_enabled) {
			/* this should not happen, so we will just ignore it */
		} else {
			vub300->irq_enabled = 1;
			vub300_queue_poll_work(vub300, 0);
		}
		mutex_unlock(&vub300->irq_mutex);
	} else {
		vub300->irq_enabled = 0;
	}
	kref_put(&vub300->kref, vub300_delete);
}

void vub300_init_card(struct mmc_host *mmc, struct mmc_card *card)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 = mmc_priv(mmc);
	dev_info(&vub300->udev->dev, "NO host QUIRKS for this card\n");
}

static struct mmc_host_ops vub300_mmc_ops = {
	.request = vub300_mmc_request,
	.set_ios = vub300_mmc_set_ios,
	.get_ro = vub300_mmc_get_ro,
	.enable_sdio_irq = vub300_enable_sdio_irq,
	.init_card = vub300_init_card,
};

static int vub300_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{				/* NOT irq */
	struct vub300_mmc_host *vub300;
	struct usb_host_interface *iface_desc;
	struct usb_device *udev = usb_get_dev(interface_to_usbdev(interface));
	int i;
	int retval = -ENOMEM;
	struct urb *command_out_urb;
	struct urb *command_res_urb;
	struct mmc_host *mmc;
	char manufacturer[48];
	char product[32];
	char serial_number[32];
	usb_string(udev, udev->descriptor.iManufacturer, manufacturer,
		   sizeof(manufacturer));
	usb_string(udev, udev->descriptor.iProduct, product, sizeof(product));
	usb_string(udev, udev->descriptor.iSerialNumber, serial_number,
		   sizeof(serial_number));
	dev_info(&udev->dev, "probing VID:PID(%04X:%04X) %s %s %s\n",
		 udev->descriptor.idVendor, udev->descriptor.idProduct,
		 manufacturer, product, serial_number);
	command_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!command_out_urb) {
		retval = -ENOMEM;
		dev_err(&udev->dev, "not enough memory for command_out_urb\n");
		goto error0;
	}
	command_res_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!command_res_urb) {
		retval = -ENOMEM;
		dev_err(&udev->dev, "not enough memory for command_res_urb\n");
		goto error1;
	}
	/* this also allocates memory for our VUB300 mmc host device */
	mmc = mmc_alloc_host(sizeof(struct vub300_mmc_host), &udev->dev);
	if (!mmc) {
		retval = -ENOMEM;
		dev_err(&udev->dev, "not enough memory for the mmc_host\n");
		goto error4;
	}
	/* MMC core transfer sizes tunable parameters */
	mmc->caps = 0;
	if (!force_1_bit_data_xfers)
		mmc->caps |= MMC_CAP_4_BIT_DATA;
	if (!force_polling_for_irqs)
		mmc->caps |= MMC_CAP_SDIO_IRQ;
	mmc->caps &= ~MMC_CAP_NEEDS_POLL;
	/*
	 * MMC_CAP_NEEDS_POLL causes core.c:mmc_rescan() to poll
	 * for devices which results in spurious CMD7's being
	 * issued which stops some SDIO cards from working
	 */
	if (limit_speed_to_24_MHz) {
		mmc->caps |= MMC_CAP_MMC_HIGHSPEED;
		mmc->caps |= MMC_CAP_SD_HIGHSPEED;
		mmc->f_max = 24000000;
		dev_info(&udev->dev, "limiting SDIO speed to 24_MHz\n");
	} else {
		mmc->caps |= MMC_CAP_MMC_HIGHSPEED;
		mmc->caps |= MMC_CAP_SD_HIGHSPEED;
		mmc->f_max = 48000000;
	}
	mmc->f_min = 200000;
	mmc->max_blk_count = 511;
	mmc->max_blk_size = 512;
	mmc->max_segs = 128;
	if (force_max_req_size)
		mmc->max_req_size = force_max_req_size * 1024;
	else
		mmc->max_req_size = 64 * 1024;
	mmc->max_seg_size = mmc->max_req_size;
	mmc->ocr_avail = 0;
	mmc->ocr_avail |= MMC_VDD_165_195;
	mmc->ocr_avail |= MMC_VDD_20_21;
	mmc->ocr_avail |= MMC_VDD_21_22;
	mmc->ocr_avail |= MMC_VDD_22_23;
	mmc->ocr_avail |= MMC_VDD_23_24;
	mmc->ocr_avail |= MMC_VDD_24_25;
	mmc->ocr_avail |= MMC_VDD_25_26;
	mmc->ocr_avail |= MMC_VDD_26_27;
	mmc->ocr_avail |= MMC_VDD_27_28;
	mmc->ocr_avail |= MMC_VDD_28_29;
	mmc->ocr_avail |= MMC_VDD_29_30;
	mmc->ocr_avail |= MMC_VDD_30_31;
	mmc->ocr_avail |= MMC_VDD_31_32;
	mmc->ocr_avail |= MMC_VDD_32_33;
	mmc->ocr_avail |= MMC_VDD_33_34;
	mmc->ocr_avail |= MMC_VDD_34_35;
	mmc->ocr_avail |= MMC_VDD_35_36;
	mmc->ops = &vub300_mmc_ops;
	vub300 = mmc_priv(mmc);
	vub300->mmc = mmc;
	vub300->card_powered = 0;
	vub300->bus_width = 0;
	vub300->cmnd.head.block_size[0] = 0x00;
	vub300->cmnd.head.block_size[1] = 0x00;
	vub300->app_spec = 0;
	mutex_init(&vub300->cmd_mutex);
	mutex_init(&vub300->irq_mutex);
	vub300->command_out_urb = command_out_urb;
	vub300->command_res_urb = command_res_urb;
	vub300->usb_timed_out = 0;
	vub300->dynamic_register_count = 0;

	for (i = 0; i < ARRAY_SIZE(vub300->fn); i++) {
		vub300->fn[i].offload_point = 0;
		vub300->fn[i].offload_count = 0;
	}

	vub300->total_offload_count = 0;
	vub300->irq_enabled = 0;
	vub300->irq_disabled = 0;
	vub300->irqs_queued = 0;

	for (i = 0; i < ARRAY_SIZE(vub300->sdio_register); i++)
		vub300->sdio_register[i++].activate = 0;

	vub300->udev = udev;
	vub300->interface = interface;
	vub300->cmnd_res_ep = 0;
	vub300->cmnd_out_ep = 0;
	vub300->data_inp_ep = 0;
	vub300->data_out_ep = 0;

	for (i = 0; i < ARRAY_SIZE(vub300->fbs); i++)
		vub300->fbs[i] = 512;

	/*
	 *      set up the endpoint information
	 *
	 * use the first pair of bulk-in and bulk-out
	 *     endpoints for Command/Response+Interrupt
	 *
	 * use the second pair of bulk-in and bulk-out
	 *     endpoints for Data In/Out
	 */
	vub300->large_usb_packets = 0;
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		struct usb_endpoint_descriptor *endpoint =
			&iface_desc->endpoint[i].desc;
		dev_info(&vub300->udev->dev,
			 "vub300 testing %s EndPoint(%d) %02X\n",
			 usb_endpoint_is_bulk_in(endpoint) ? "BULK IN" :
			 usb_endpoint_is_bulk_out(endpoint) ? "BULK OUT" :
			 "UNKNOWN", i, endpoint->bEndpointAddress);
		if (endpoint->wMaxPacketSize > 64)
			vub300->large_usb_packets = 1;
		if (usb_endpoint_is_bulk_in(endpoint)) {
			if (!vub300->cmnd_res_ep) {
				vub300->cmnd_res_ep =
					endpoint->bEndpointAddress;
			} else if (!vub300->data_inp_ep) {
				vub300->data_inp_ep =
					endpoint->bEndpointAddress;
			} else {
				dev_warn(&vub300->udev->dev,
					 "ignoring"
					 " unexpected bulk_in endpoint");
			}
		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			if (!vub300->cmnd_out_ep) {
				vub300->cmnd_out_ep =
					endpoint->bEndpointAddress;
			} else if (!vub300->data_out_ep) {
				vub300->data_out_ep =
					endpoint->bEndpointAddress;
			} else {
				dev_warn(&vub300->udev->dev,
					 "ignoring"
					 " unexpected bulk_out endpoint");
			}
		} else {
			dev_warn(&vub300->udev->dev,
				 "vub300 ignoring EndPoint(%d) %02X", i,
				 endpoint->bEndpointAddress);
		}
	}
	if (vub300->cmnd_res_ep && vub300->cmnd_out_ep &&
	    vub300->data_inp_ep && vub300->data_out_ep) {
		dev_info(&vub300->udev->dev,
			 "vub300 %s packets"
			 " using EndPoints %02X %02X %02X %02X\n",
			 vub300->large_usb_packets ? "LARGE" : "SMALL",
			 vub300->cmnd_out_ep, vub300->cmnd_res_ep,
			 vub300->data_out_ep, vub300->data_inp_ep);
		/* we have the expected EndPoints */
	} else {
		dev_err(&vub300->udev->dev,
		    "Could not find two sets of bulk-in/out endpoint pairs\n");
		retval = -EINVAL;
		goto error5;
	}
	retval =
		usb_control_msg(vub300->udev, usb_rcvctrlpipe(vub300->udev, 0),
				GET_HC_INF0,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x0000, 0x0000, &vub300->hc_info,
				sizeof(vub300->hc_info), HZ);
	if (retval < 0)
		goto error5;
	retval =
		usb_control_msg(vub300->udev, usb_rcvctrlpipe(vub300->udev, 0),
				SET_ROM_WAIT_STATES,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				firmware_rom_wait_states, 0x0000, NULL, 0, HZ);
	if (retval < 0)
		goto error5;
	dev_info(&vub300->udev->dev,
		 "operating_mode = %s %s %d MHz %s %d byte USB packets\n",
		 (mmc->caps & MMC_CAP_SDIO_IRQ) ? "IRQs" : "POLL",
		 (mmc->caps & MMC_CAP_4_BIT_DATA) ? "4-bit" : "1-bit",
		 mmc->f_max / 1000000,
		 pad_input_to_usb_pkt ? "padding input data to" : "with",
		 vub300->large_usb_packets ? 512 : 64);
	retval =
		usb_control_msg(vub300->udev, usb_rcvctrlpipe(vub300->udev, 0),
				GET_SYSTEM_PORT_STATUS,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x0000, 0x0000, &vub300->system_port_status,
				sizeof(vub300->system_port_status), HZ);
	if (retval < 0) {
		goto error4;
	} else if (sizeof(vub300->system_port_status) == retval) {
		vub300->card_present =
			(0x0001 & vub300->system_port_status.port_flags) ? 1 : 0;
		vub300->read_only =
			(0x0010 & vub300->system_port_status.port_flags) ? 1 : 0;
	} else {
		goto error4;
	}
	usb_set_intfdata(interface, vub300);
	INIT_DELAYED_WORK(&vub300->pollwork, vub300_pollwork_thread);
	INIT_WORK(&vub300->cmndwork, vub300_cmndwork_thread);
	INIT_WORK(&vub300->deadwork, vub300_deadwork_thread);
	kref_init(&vub300->kref);
	init_timer(&vub300->sg_transfer_timer);
	vub300->sg_transfer_timer.data = (unsigned long)vub300;
	vub300->sg_transfer_timer.function = vub300_sg_timed_out;
	kref_get(&vub300->kref);
	init_timer(&vub300->inactivity_timer);
	vub300->inactivity_timer.data = (unsigned long)vub300;
	vub300->inactivity_timer.function = vub300_inactivity_timer_expired;
	vub300->inactivity_timer.expires = jiffies + HZ;
	add_timer(&vub300->inactivity_timer);
	if (vub300->card_present)
		dev_info(&vub300->udev->dev,
			 "USB vub300 remote SDIO host controller[%d]"
			 "connected with SD/SDIO card inserted\n",
			 interface_to_InterfaceNumber(interface));
	else
		dev_info(&vub300->udev->dev,
			 "USB vub300 remote SDIO host controller[%d]"
			 "connected with no SD/SDIO card inserted\n",
			 interface_to_InterfaceNumber(interface));
	mmc_add_host(mmc);
	return 0;
error5:
	mmc_free_host(mmc);
	/*
	 * and hence also frees vub300
	 * which is contained at the end of struct mmc
	 */
error4:
	usb_free_urb(command_out_urb);
error1:
	usb_free_urb(command_res_urb);
error0:
	return retval;
}

static void vub300_disconnect(struct usb_interface *interface)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 = usb_get_intfdata(interface);
	if (!vub300 || !vub300->mmc) {
		return;
	} else {
		struct mmc_host *mmc = vub300->mmc;
		if (!vub300->mmc) {
			return;
		} else {
			int ifnum = interface_to_InterfaceNumber(interface);
			usb_set_intfdata(interface, NULL);
			/* prevent more I/O from starting */
			vub300->interface = NULL;
			kref_put(&vub300->kref, vub300_delete);
			mmc_remove_host(mmc);
			pr_info("USB vub300 remote SDIO host controller[%d]"
				" now disconnected", ifnum);
			return;
		}
	}
}

#ifdef CONFIG_PM
static int vub300_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct vub300_mmc_host *vub300 = usb_get_intfdata(intf);
	if (!vub300 || !vub300->mmc) {
		return 0;
	} else {
		struct mmc_host *mmc = vub300->mmc;
		mmc_suspend_host(mmc);
		return 0;
	}
}

static int vub300_resume(struct usb_interface *intf)
{
	struct vub300_mmc_host *vub300 = usb_get_intfdata(intf);
	if (!vub300 || !vub300->mmc) {
		return 0;
	} else {
		struct mmc_host *mmc = vub300->mmc;
		mmc_resume_host(mmc);
		return 0;
	}
}
#else
#define vub300_suspend NULL
#define vub300_resume NULL
#endif
static int vub300_pre_reset(struct usb_interface *intf)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 = usb_get_intfdata(intf);
	mutex_lock(&vub300->cmd_mutex);
	return 0;
}

static int vub300_post_reset(struct usb_interface *intf)
{				/* NOT irq */
	struct vub300_mmc_host *vub300 = usb_get_intfdata(intf);
	/* we are sure no URBs are active - no locking needed */
	vub300->errors = -EPIPE;
	mutex_unlock(&vub300->cmd_mutex);
	return 0;
}

static struct usb_driver vub300_driver = {
	.name = "vub300",
	.probe = vub300_probe,
	.disconnect = vub300_disconnect,
	.suspend = vub300_suspend,
	.resume = vub300_resume,
	.pre_reset = vub300_pre_reset,
	.post_reset = vub300_post_reset,
	.id_table = vub300_table,
	.supports_autosuspend = 1,
};

static int __init vub300_init(void)
{				/* NOT irq */
	int result;

	pr_info("VUB300 Driver rom wait states = %02X irqpoll timeout = %04X",
		firmware_rom_wait_states, 0x0FFFF & firmware_irqpoll_timeout);
	cmndworkqueue = create_singlethread_workqueue("kvub300c");
	if (!cmndworkqueue) {
		pr_err("not enough memory for the REQUEST workqueue");
		result = -ENOMEM;
		goto out1;
	}
	pollworkqueue = create_singlethread_workqueue("kvub300p");
	if (!pollworkqueue) {
		pr_err("not enough memory for the IRQPOLL workqueue");
		result = -ENOMEM;
		goto out2;
	}
	deadworkqueue = create_singlethread_workqueue("kvub300d");
	if (!deadworkqueue) {
		pr_err("not enough memory for the EXPIRED workqueue");
		result = -ENOMEM;
		goto out3;
	}
	result = usb_register(&vub300_driver);
	if (result) {
		pr_err("usb_register failed. Error number %d", result);
		goto out4;
	}
	return 0;
out4:
	destroy_workqueue(deadworkqueue);
out3:
	destroy_workqueue(pollworkqueue);
out2:
	destroy_workqueue(cmndworkqueue);
out1:
	return result;
}

static void __exit vub300_exit(void)
{
	usb_deregister(&vub300_driver);
	flush_workqueue(cmndworkqueue);
	flush_workqueue(pollworkqueue);
	flush_workqueue(deadworkqueue);
	destroy_workqueue(cmndworkqueue);
	destroy_workqueue(pollworkqueue);
	destroy_workqueue(deadworkqueue);
}

module_init(vub300_init);
module_exit(vub300_exit);

MODULE_AUTHOR("Tony Olech <tony.olech@elandigitalsystems.com>");
MODULE_DESCRIPTION("VUB300 USB to SD/MMC/SDIO adapter driver");
MODULE_LICENSE("GPL");
