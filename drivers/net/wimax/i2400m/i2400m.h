/*
 * Intel Wireless WiMAX Connection 2400m
 * Declarations for bus-generic internal APIs
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 * Yanir Lubetkin <yanirx.lubetkin@intel.com>
 *  - Initial implementation
 *
 *
 * GENERAL DRIVER ARCHITECTURE
 *
 * The i2400m driver is split in the following two major parts:
 *
 *  - bus specific driver
 *  - bus generic driver (this part)
 *
 * The bus specific driver sets up stuff specific to the bus the
 * device is connected to (USB, SDIO, PCI, tam-tam...non-authoritative
 * nor binding list) which is basically the device-model management
 * (probe/disconnect, etc), moving data from device to kernel and
 * back, doing the power saving details and reseting the device.
 *
 * For details on each bus-specific driver, see it's include file,
 * i2400m-BUSNAME.h
 *
 * The bus-generic functionality break up is:
 *
 *  - Firmware upload: fw.c - takes care of uploading firmware to the
 *        device. bus-specific driver just needs to provides a way to
 *        execute boot-mode commands and to reset the device.
 *
 *  - RX handling: rx.c - receives data from the bus-specific code and
 *        feeds it to the network or WiMAX stack or uses it to modify
 *        the driver state. bus-specific driver only has to receive
 *        frames and pass them to this module.
 *
 *  - TX handling: tx.c - manages the TX FIFO queue and provides means
 *        for the bus-specific TX code to pull data from the FIFO
 *        queue. bus-specific code just pulls frames from this module
 *        to sends them to the device.
 *
 *  - netdev glue: netdev.c - interface with Linux networking
 *        stack. Pass around data frames, and configure when the
 *        device is up and running or shutdown (through ifconfig up /
 *        down). Bus-generic only.
 *
 *  - control ops: control.c - implements various commmands for
 *        controlling the device. bus-generic only.
 *
 *  - device model glue: driver.c - implements helpers for the
 *        device-model glue done by the bus-specific layer
 *        (setup/release the driver resources), turning the device on
 *        and off, handling the device reboots/resets and a few simple
 *        WiMAX stack ops.
 *
 * Code is also broken up in linux-glue / device-glue.
 *
 * Linux glue contains functions that deal mostly with gluing with the
 * rest of the Linux kernel.
 *
 * Device-glue are functions that deal mostly with the way the device
 * does things and talk the device's language.
 *
 * device-glue code is licensed BSD so other open source OSes can take
 * it to implement their drivers.
 *
 *
 * APIs AND HEADER FILES
 *
 * This bus generic code exports three APIs:
 *
 *  - HDI (host-device interface) definitions common to all busses
 *    (include/linux/wimax/i2400m.h); these can be also used by user
 *    space code.
 *  - internal API for the bus-generic code
 *  - external API for the bus-specific drivers
 *
 *
 * LIFE CYCLE:
 *
 * When the bus-specific driver probes, it allocates a network device
 * with enough space for it's data structue, that must contain a
 * &struct i2400m at the top.
 *
 * On probe, it needs to fill the i2400m members marked as [fill], as
 * well as i2400m->wimax_dev.net_dev and call i2400m_setup(). The
 * i2400m driver will only register with the WiMAX and network stacks;
 * the only access done to the device is to read the MAC address so we
 * can register a network device. This calls i2400m_dev_start() to
 * load firmware, setup communication with the device and configure it
 * for operation.
 *
 * At this point, control and data communications are possible.
 *
 * On disconnect/driver unload, the bus-specific disconnect function
 * calls i2400m_release() to undo i2400m_setup(). i2400m_dev_stop()
 * shuts the firmware down and releases resources uses to communicate
 * with the device.
 *
 * While the device is up, it might reset. The bus-specific driver has
 * to catch that situation and call i2400m_dev_reset_handle() to deal
 * with it (reset the internal driver structures and go back to square
 * one).
 */

#ifndef __I2400M_H__
#define __I2400M_H__

#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/completion.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>
#include <net/wimax.h>
#include <linux/wimax/i2400m.h>
#include <asm/byteorder.h>

/* Misc constants */
enum {
	/* Firmware uploading */
	I2400M_BOOT_RETRIES = 3,
	/* Size of the Boot Mode Command buffer */
	I2400M_BM_CMD_BUF_SIZE = 16 * 1024,
	I2400M_BM_ACK_BUF_SIZE = 256,
};


/**
 * i2400m_reset_type - methods to reset a device
 *
 * @I2400M_RT_WARM: Reset without device disconnection, device handles
 *     are kept valid but state is back to power on, with firmware
 *     re-uploaded.
 * @I2400M_RT_COLD: Tell the device to disconnect itself from the bus
 *     and reconnect. Renders all device handles invalid.
 * @I2400M_RT_BUS: Tells the bus to reset the device; last measure
 *     used when both types above don't work.
 */
enum i2400m_reset_type {
	I2400M_RT_WARM,	/* first measure */
	I2400M_RT_COLD,	/* second measure */
	I2400M_RT_BUS,	/* call in artillery */
};

struct i2400m_reset_ctx;
struct i2400m_roq;

/**
 * struct i2400m - descriptor for an Intel 2400m
 *
 * Members marked with [fill] must be filled out/initialized before
 * calling i2400m_setup().
 *
 * @bus_tx_block_size: [fill] SDIO imposes a 256 block size, USB 16,
 *     so we have a tx_blk_size variable that the bus layer sets to
 *     tell the engine how much of that we need.
 *
 * @bus_pl_size_max: [fill] Maximum payload size.
 *
 * @bus_dev_start: [fill] Function called by the bus-generic code
 *     [i2400m_dev_start()] to setup the bus-specific communications
 *     to the the device. See LIFE CYCLE above.
 *
 *     NOTE: Doesn't need to upload the firmware, as that is taken
 *     care of by the bus-generic code.
 *
 * @bus_dev_stop: [fill] Function called by the bus-generic code
 *     [i2400m_dev_stop()] to shutdown the bus-specific communications
 *     to the the device. See LIFE CYCLE above.
 *
 *     This function does not need to reset the device, just tear down
 *     all the host resources created to  handle communication with
 *     the device.
 *
 * @bus_tx_kick: [fill] Function called by the bus-generic code to let
 *     the bus-specific code know that there is data available in the
 *     TX FIFO for transmission to the device.
 *
 *     This function cannot sleep.
 *
 * @bus_reset: [fill] Function called by the bus-generic code to reset
 *     the device in in various ways. Doesn't need to wait for the
 *     reset to finish.
 *
 *     If warm or cold reset fail, this function is expected to do a
 *     bus-specific reset (eg: USB reset) to get the device to a
 *     working state (even if it implies device disconecction).
 *
 *     Note the warm reset is used by the firmware uploader to
 *     reinitialize the device.
 *
 *     IMPORTANT: this is called very early in the device setup
 *     process, so it cannot rely on common infrastructure being laid
 *     out.
 *
 * @bus_bm_cmd_send: [fill] Function called to send a boot-mode
 *     command. Flags are defined in 'enum i2400m_bm_cmd_flags'. This
 *     is synchronous and has to return 0 if ok or < 0 errno code in
 *     any error condition.
 *
 * @bus_bm_wait_for_ack: [fill] Function called to wait for a
 *     boot-mode notification (that can be a response to a previously
 *     issued command or an asynchronous one). Will read until all the
 *     indicated size is read or timeout. Reading more or less data
 *     than asked for is an error condition. Return 0 if ok, < 0 errno
 *     code on error.
 *
 *     The caller to this function will check if the response is a
 *     barker that indicates the device going into reset mode.
 *
 * @bus_fw_names: [fill] a NULL-terminated array with the names of the
 *     firmware images to try loading. This is made a list so we can
 *     support backward compatibility of firmware releases (eg: if we
 *     can't find the default v1.4, we try v1.3). In general, the name
 *     should be i2400m-fw-X-VERSION.sbcf, where X is the bus name.
 *     The list is tried in order and the first one that loads is
 *     used. The fw loader will set i2400m->fw_name to point to the
 *     active firmware image.
 *
 * @bus_bm_mac_addr_impaired: [fill] Set to true if the device's MAC
 *     address provided in boot mode is kind of broken and needs to
 *     be re-read later on.
 *
 *
 * @wimax_dev: WiMAX generic device for linkage into the kernel WiMAX
 *     stack. Due to the way a net_device is allocated, we need to
 *     force this to be the first field so that we can get from
 *     netdev_priv() the right pointer.
 *
 * @rx_reorder: 1 if RX reordering is enabled; this can only be
 *     set at probe time.
 *
 * @state: device's state (as reported by it)
 *
 * @state_wq: waitqueue that is woken up whenever the state changes
 *
 * @tx_lock: spinlock to protect TX members
 *
 * @tx_buf: FIFO buffer for TX; we queue data here
 *
 * @tx_in: FIFO index for incoming data. Note this doesn't wrap around
 *     and it is always greater than @tx_out.
 *
 * @tx_out: FIFO index for outgoing data
 *
 * @tx_msg: current TX message that is active in the FIFO for
 *     appending payloads.
 *
 * @tx_sequence: current sequence number for TX messages from the
 *     device to the host.
 *
 * @tx_msg_size: size of the current message being transmitted by the
 *     bus-specific code.
 *
 * @tx_pl_num: total number of payloads sent
 *
 * @tx_pl_max: maximum number of payloads sent in a TX message
 *
 * @tx_pl_min: minimum number of payloads sent in a TX message
 *
 * @tx_num: number of TX messages sent
 *
 * @tx_size_acc: number of bytes in all TX messages sent
 *     (this is different to net_dev's statistics as it also counts
 *     control messages).
 *
 * @tx_size_min: smallest TX message sent.
 *
 * @tx_size_max: biggest TX message sent.
 *
 * @rx_lock: spinlock to protect RX members
 *
 * @rx_pl_num: total number of payloads received
 *
 * @rx_pl_max: maximum number of payloads received in a RX message
 *
 * @rx_pl_min: minimum number of payloads received in a RX message
 *
 * @rx_num: number of RX messages received
 *
 * @rx_size_acc: number of bytes in all RX messages received
 *     (this is different to net_dev's statistics as it also counts
 *     control messages).
 *
 * @rx_size_min: smallest RX message received.
 *
 * @rx_size_max: buggest RX message received.
 *
 * @rx_roq: RX ReOrder queues. (fw >= v1.4) When packets are received
 *     out of order, the device will ask the driver to hold certain
 *     packets until the ones that are received out of order can be
 *     delivered. Then the driver can release them to the host. See
 *     drivers/net/i2400m/rx.c for details.
 *
 * @src_mac_addr: MAC address used to make ethernet packets be coming
 *     from. This is generated at i2400m_setup() time and used during
 *     the life cycle of the instance. See i2400m_fake_eth_header().
 *
 * @init_mutex: Mutex used for serializing the device bringup
 *     sequence; this way if the device reboots in the middle, we
 *     don't try to do a bringup again while we are tearing down the
 *     one that failed.
 *
 *     Can't reuse @msg_mutex because from within the bringup sequence
 *     we need to send messages to the device and thus use @msg_mutex.
 *
 * @msg_mutex: mutex used to send control commands to the device (we
 *     only allow one at a time, per host-device interface design).
 *
 * @msg_completion: used to wait for an ack to a control command sent
 *     to the device.
 *
 * @ack_skb: used to store the actual ack to a control command if the
 *     reception of the command was successful. Otherwise, a ERR_PTR()
 *     errno code that indicates what failed with the ack reception.
 *
 *     Only valid after @msg_completion is woken up. Only updateable
 *     if @msg_completion is armed. Only touched by
 *     i2400m_msg_to_dev().
 *
 *     Protected by @rx_lock. In theory the command execution flow is
 *     sequential, but in case the device sends an out-of-phase or
 *     very delayed response, we need to avoid it trampling current
 *     execution.
 *
 * @bm_cmd_buf: boot mode command buffer for composing firmware upload
 *     commands.
 *
 *     USB can't r/w to stack, vmalloc, etc...as well, we end up
 *     having to alloc/free a lot to compose commands, so we use these
 *     for stagging and not having to realloc all the time.
 *
 *     This assumes the code always runs serialized. Only one thread
 *     can call i2400m_bm_cmd() at the same time.
 *
 * @bm_ack_buf: boot mode acknoledge buffer for staging reception of
 *     responses to commands.
 *
 *     See @bm_cmd_buf.
 *
 * @work_queue: work queue for processing device reports. This
 *     workqueue cannot be used for processing TX or RX to the device,
 *     as from it we'll process device reports, which might require
 *     further communication with the device.
 *
 * @debugfs_dentry: hookup for debugfs files.
 *     These have to be in a separate directory, a child of
 *     (wimax_dev->debugfs_dentry) so they can be removed when the
 *     module unloads, as we don't keep each dentry.
 *
 * @fw_name: name of the firmware image that is currently being used.
 *
 * @fw_version: version of the firmware interface, Major.minor,
 *     encoded in the high word and low word (major << 16 | minor).
 */
struct i2400m {
	struct wimax_dev wimax_dev;	/* FIRST! See doc */

	unsigned updown:1;		/* Network device is up or down */
	unsigned boot_mode:1;		/* is the device in boot mode? */
	unsigned sboot:1;		/* signed or unsigned fw boot */
	unsigned ready:1;		/* all probing steps done */
	unsigned rx_reorder:1;		/* RX reorder is enabled */
	u8 trace_msg_from_user;		/* echo rx msgs to 'trace' pipe */
					/* typed u8 so debugfs/u8 can tweak */
	enum i2400m_system_state state;
	wait_queue_head_t state_wq;	/* Woken up when on state updates */

	size_t bus_tx_block_size;
	size_t bus_pl_size_max;
	int (*bus_dev_start)(struct i2400m *);
	void (*bus_dev_stop)(struct i2400m *);
	void (*bus_tx_kick)(struct i2400m *);
	int (*bus_reset)(struct i2400m *, enum i2400m_reset_type);
	ssize_t (*bus_bm_cmd_send)(struct i2400m *,
				   const struct i2400m_bootrom_header *,
				   size_t, int flags);
	ssize_t (*bus_bm_wait_for_ack)(struct i2400m *,
				       struct i2400m_bootrom_header *, size_t);
	const char **bus_fw_names;
	unsigned bus_bm_mac_addr_impaired:1;

	spinlock_t tx_lock;		/* protect TX state */
	void *tx_buf;
	size_t tx_in, tx_out;
	struct i2400m_msg_hdr *tx_msg;
	size_t tx_sequence, tx_msg_size;
	/* TX stats */
	unsigned tx_pl_num, tx_pl_max, tx_pl_min,
		tx_num, tx_size_acc, tx_size_min, tx_size_max;

	/* RX stuff */
	spinlock_t rx_lock;		/* protect RX state */
	unsigned rx_pl_num, rx_pl_max, rx_pl_min,
		rx_num, rx_size_acc, rx_size_min, rx_size_max;
	struct i2400m_roq *rx_roq;	/* not under rx_lock! */
	u8 src_mac_addr[ETH_HLEN];

	struct mutex msg_mutex;		/* serialize command execution */
	struct completion msg_completion;
	struct sk_buff *ack_skb;	/* protected by rx_lock */

	void *bm_ack_buf;		/* for receiving acks over USB */
	void *bm_cmd_buf;		/* for issuing commands over USB */

	struct workqueue_struct *work_queue;

	struct mutex init_mutex;	/* protect bringup seq */
	struct i2400m_reset_ctx *reset_ctx;	/* protected by init_mutex */

	struct work_struct wake_tx_ws;
	struct sk_buff *wake_tx_skb;

	struct dentry *debugfs_dentry;
	const char *fw_name;		/* name of the current firmware image */
	unsigned long fw_version;	/* version of the firmware interface */
};


/*
 * Initialize a 'struct i2400m' from all zeroes
 *
 * This is a bus-generic API call.
 */
static inline
void i2400m_init(struct i2400m *i2400m)
{
	wimax_dev_init(&i2400m->wimax_dev);

	i2400m->boot_mode = 1;
	i2400m->rx_reorder = 1;
	init_waitqueue_head(&i2400m->state_wq);

	spin_lock_init(&i2400m->tx_lock);
	i2400m->tx_pl_min = UINT_MAX;
	i2400m->tx_size_min = UINT_MAX;

	spin_lock_init(&i2400m->rx_lock);
	i2400m->rx_pl_min = UINT_MAX;
	i2400m->rx_size_min = UINT_MAX;

	mutex_init(&i2400m->msg_mutex);
	init_completion(&i2400m->msg_completion);

	mutex_init(&i2400m->init_mutex);
	/* wake_tx_ws is initialized in i2400m_tx_setup() */
}


/*
 * Bus-generic internal APIs
 * -------------------------
 */

static inline
struct i2400m *wimax_dev_to_i2400m(struct wimax_dev *wimax_dev)
{
	return container_of(wimax_dev, struct i2400m, wimax_dev);
}

static inline
struct i2400m *net_dev_to_i2400m(struct net_device *net_dev)
{
	return wimax_dev_to_i2400m(netdev_priv(net_dev));
}

/*
 * Boot mode support
 */

/**
 * i2400m_bm_cmd_flags - flags to i2400m_bm_cmd()
 *
 * @I2400M_BM_CMD_RAW: send the command block as-is, without doing any
 *     extra processing for adding CRC.
 */
enum i2400m_bm_cmd_flags {
	I2400M_BM_CMD_RAW	= 1 << 2,
};

/**
 * i2400m_bri - Boot-ROM indicators
 *
 * Flags for i2400m_bootrom_init() and i2400m_dev_bootstrap() [which
 * are passed from things like i2400m_setup()]. Can be combined with
 * |.
 *
 * @I2400M_BRI_SOFT: The device rebooted already and a reboot
 *     barker received, proceed directly to ack the boot sequence.
 * @I2400M_BRI_NO_REBOOT: Do not reboot the device and proceed
 *     directly to wait for a reboot barker from the device.
 * @I2400M_BRI_MAC_REINIT: We need to reinitialize the boot
 *     rom after reading the MAC adress. This is quite a dirty hack,
 *     if you ask me -- the device requires the bootrom to be
 *     intialized after reading the MAC address.
 */
enum i2400m_bri {
	I2400M_BRI_SOFT       = 1 << 1,
	I2400M_BRI_NO_REBOOT  = 1 << 2,
	I2400M_BRI_MAC_REINIT = 1 << 3,
};

extern void i2400m_bm_cmd_prepare(struct i2400m_bootrom_header *);
extern int i2400m_dev_bootstrap(struct i2400m *, enum i2400m_bri);
extern int i2400m_read_mac_addr(struct i2400m *);
extern int i2400m_bootrom_init(struct i2400m *, enum i2400m_bri);

/* Make/grok boot-rom header commands */

static inline
__le32 i2400m_brh_command(enum i2400m_brh_opcode opcode, unsigned use_checksum,
			  unsigned direct_access)
{
	return cpu_to_le32(
		I2400M_BRH_SIGNATURE
		| (direct_access ? I2400M_BRH_DIRECT_ACCESS : 0)
		| I2400M_BRH_RESPONSE_REQUIRED /* response always required */
		| (use_checksum ? I2400M_BRH_USE_CHECKSUM : 0)
		| (opcode & I2400M_BRH_OPCODE_MASK));
}

static inline
void i2400m_brh_set_opcode(struct i2400m_bootrom_header *hdr,
			   enum i2400m_brh_opcode opcode)
{
	hdr->command = cpu_to_le32(
		(le32_to_cpu(hdr->command) & ~I2400M_BRH_OPCODE_MASK)
		| (opcode & I2400M_BRH_OPCODE_MASK));
}

static inline
unsigned i2400m_brh_get_opcode(const struct i2400m_bootrom_header *hdr)
{
	return le32_to_cpu(hdr->command) & I2400M_BRH_OPCODE_MASK;
}

static inline
unsigned i2400m_brh_get_response(const struct i2400m_bootrom_header *hdr)
{
	return (le32_to_cpu(hdr->command) & I2400M_BRH_RESPONSE_MASK)
		>> I2400M_BRH_RESPONSE_SHIFT;
}

static inline
unsigned i2400m_brh_get_use_checksum(const struct i2400m_bootrom_header *hdr)
{
	return le32_to_cpu(hdr->command) & I2400M_BRH_USE_CHECKSUM;
}

static inline
unsigned i2400m_brh_get_response_required(
	const struct i2400m_bootrom_header *hdr)
{
	return le32_to_cpu(hdr->command) & I2400M_BRH_RESPONSE_REQUIRED;
}

static inline
unsigned i2400m_brh_get_direct_access(const struct i2400m_bootrom_header *hdr)
{
	return le32_to_cpu(hdr->command) & I2400M_BRH_DIRECT_ACCESS;
}

static inline
unsigned i2400m_brh_get_signature(const struct i2400m_bootrom_header *hdr)
{
	return (le32_to_cpu(hdr->command) & I2400M_BRH_SIGNATURE_MASK)
		>> I2400M_BRH_SIGNATURE_SHIFT;
}


/*
 * Driver / device setup and internal functions
 */
extern void i2400m_netdev_setup(struct net_device *net_dev);
extern int i2400m_sysfs_setup(struct device_driver *);
extern void i2400m_sysfs_release(struct device_driver *);
extern int i2400m_tx_setup(struct i2400m *);
extern void i2400m_wake_tx_work(struct work_struct *);
extern void i2400m_tx_release(struct i2400m *);

extern int i2400m_rx_setup(struct i2400m *);
extern void i2400m_rx_release(struct i2400m *);

extern void i2400m_net_rx(struct i2400m *, struct sk_buff *, unsigned,
			  const void *, int);
extern void i2400m_net_erx(struct i2400m *, struct sk_buff *,
			   enum i2400m_cs);
enum i2400m_pt;
extern int i2400m_tx(struct i2400m *, const void *, size_t, enum i2400m_pt);

#ifdef CONFIG_DEBUG_FS
extern int i2400m_debugfs_add(struct i2400m *);
extern void i2400m_debugfs_rm(struct i2400m *);
#else
static inline int i2400m_debugfs_add(struct i2400m *i2400m)
{
	return 0;
}
static inline void i2400m_debugfs_rm(struct i2400m *i2400m) {}
#endif

/* Called by _dev_start()/_dev_stop() to initialize the device itself */
extern int i2400m_dev_initialize(struct i2400m *);
extern void i2400m_dev_shutdown(struct i2400m *);

extern struct attribute_group i2400m_dev_attr_group;

extern int i2400m_schedule_work(struct i2400m *,
				void (*)(struct work_struct *), gfp_t);

/* HDI message's payload description handling */

static inline
size_t i2400m_pld_size(const struct i2400m_pld *pld)
{
	return I2400M_PLD_SIZE_MASK & le32_to_cpu(pld->val);
}

static inline
enum i2400m_pt i2400m_pld_type(const struct i2400m_pld *pld)
{
	return (I2400M_PLD_TYPE_MASK & le32_to_cpu(pld->val))
		>> I2400M_PLD_TYPE_SHIFT;
}

static inline
void i2400m_pld_set(struct i2400m_pld *pld, size_t size,
		    enum i2400m_pt type)
{
	pld->val = cpu_to_le32(
		((type << I2400M_PLD_TYPE_SHIFT) & I2400M_PLD_TYPE_MASK)
		|  (size & I2400M_PLD_SIZE_MASK));
}


/*
 * API for the bus-specific drivers
 * --------------------------------
 */

static inline
struct i2400m *i2400m_get(struct i2400m *i2400m)
{
	dev_hold(i2400m->wimax_dev.net_dev);
	return i2400m;
}

static inline
void i2400m_put(struct i2400m *i2400m)
{
	dev_put(i2400m->wimax_dev.net_dev);
}

extern int i2400m_dev_reset_handle(struct i2400m *);

/*
 * _setup()/_release() are called by the probe/disconnect functions of
 * the bus-specific drivers.
 */
extern int i2400m_setup(struct i2400m *, enum i2400m_bri bm_flags);
extern void i2400m_release(struct i2400m *);

extern int i2400m_rx(struct i2400m *, struct sk_buff *);
extern struct i2400m_msg_hdr *i2400m_tx_msg_get(struct i2400m *, size_t *);
extern void i2400m_tx_msg_sent(struct i2400m *);

static const __le32 i2400m_NBOOT_BARKER[4] = {
	cpu_to_le32(I2400M_NBOOT_BARKER),
	cpu_to_le32(I2400M_NBOOT_BARKER),
	cpu_to_le32(I2400M_NBOOT_BARKER),
	cpu_to_le32(I2400M_NBOOT_BARKER)
};

static const __le32 i2400m_SBOOT_BARKER[4] = {
	cpu_to_le32(I2400M_SBOOT_BARKER),
	cpu_to_le32(I2400M_SBOOT_BARKER),
	cpu_to_le32(I2400M_SBOOT_BARKER),
	cpu_to_le32(I2400M_SBOOT_BARKER)
};


/*
 * Utility functions
 */

static inline
struct device *i2400m_dev(struct i2400m *i2400m)
{
	return i2400m->wimax_dev.net_dev->dev.parent;
}

/*
 * Helper for scheduling simple work functions
 *
 * This struct can get any kind of payload attached (normally in the
 * form of a struct where you pack the stuff you want to pass to the
 * _work function).
 */
struct i2400m_work {
	struct work_struct ws;
	struct i2400m *i2400m;
	u8 pl[0];
};
extern int i2400m_queue_work(struct i2400m *,
			     void (*)(struct work_struct *), gfp_t,
				const void *, size_t);

extern int i2400m_msg_check_status(const struct i2400m_l3l4_hdr *,
				   char *, size_t);
extern int i2400m_msg_size_check(struct i2400m *,
				 const struct i2400m_l3l4_hdr *, size_t);
extern struct sk_buff *i2400m_msg_to_dev(struct i2400m *, const void *, size_t);
extern void i2400m_msg_to_dev_cancel_wait(struct i2400m *, int);
extern void i2400m_msg_ack_hook(struct i2400m *,
				const struct i2400m_l3l4_hdr *, size_t);
extern void i2400m_report_hook(struct i2400m *,
			       const struct i2400m_l3l4_hdr *, size_t);
extern int i2400m_cmd_enter_powersave(struct i2400m *);
extern int i2400m_cmd_get_state(struct i2400m *);
extern int i2400m_cmd_exit_idle(struct i2400m *);
extern struct sk_buff *i2400m_get_device_info(struct i2400m *);
extern int i2400m_firmware_check(struct i2400m *);
extern int i2400m_set_init_config(struct i2400m *,
				  const struct i2400m_tlv_hdr **, size_t);
extern int i2400m_set_idle_timeout(struct i2400m *, unsigned);

static inline
struct usb_endpoint_descriptor *usb_get_epd(struct usb_interface *iface, int ep)
{
	return &iface->cur_altsetting->endpoint[ep].desc;
}

extern int i2400m_op_rfkill_sw_toggle(struct wimax_dev *,
				      enum wimax_rf_state);
extern void i2400m_report_tlv_rf_switches_status(
	struct i2400m *, const struct i2400m_tlv_rf_switches_status *);

/*
 * Helpers for firmware backwards compability
 *
 * As we aim to support at least the firmware version that was
 * released with the previous kernel/driver release, some code will be
 * conditionally executed depending on the firmware version. On each
 * release, the code to support fw releases past the last two ones
 * will be purged.
 *
 * By making it depend on this macros, it is easier to keep it a tab
 * on what has to go and what not.
 */
static inline
unsigned i2400m_le_v1_3(struct i2400m *i2400m)
{
	/* running fw is lower or v1.3 */
	return i2400m->fw_version <= 0x00090001;
}

static inline
unsigned i2400m_ge_v1_4(struct i2400m *i2400m)
{
	/* running fw is higher or v1.4 */
	return i2400m->fw_version >= 0x00090002;
}


/*
 * Do a millisecond-sleep for allowing wireshark to dump all the data
 * packets. Used only for debugging.
 */
static inline
void __i2400m_msleep(unsigned ms)
{
#if 1
#else
	msleep(ms);
#endif
}

/* Module parameters */

extern int i2400m_idle_mode_disabled;
extern int i2400m_rx_reorder_disabled;


#endif /* #ifndef __I2400M_H__ */
