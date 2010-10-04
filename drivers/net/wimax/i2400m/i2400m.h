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
 * can register a network device.
 *
 * The high-level call flow is:
 *
 * bus_probe()
 *   i2400m_setup()
 *     i2400m->bus_setup()
 *     boot rom initialization / read mac addr
 *     network / WiMAX stacks registration
 *     i2400m_dev_start()
 *       i2400m->bus_dev_start()
 *       i2400m_dev_initialize()
 *
 * The reverse applies for a disconnect() call:
 *
 * bus_disconnect()
 *   i2400m_release()
 *     i2400m_dev_stop()
 *       i2400m_dev_shutdown()
 *       i2400m->bus_dev_stop()
 *     network / WiMAX stack unregistration
 *     i2400m->bus_release()
 *
 * At this point, control and data communications are possible.
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

enum {
/* netdev interface */
	/*
	 * Out of NWG spec (R1_v1.2.2), 3.3.3 ASN Bearer Plane MTU Size
	 *
	 * The MTU is 1400 or less
	 */
	I2400M_MAX_MTU = 1400,
};

/* Misc constants */
enum {
	/* Size of the Boot Mode Command buffer */
	I2400M_BM_CMD_BUF_SIZE = 16 * 1024,
	I2400M_BM_ACK_BUF_SIZE = 256,
};

enum {
	/* Maximum number of bus reset can be retried */
	I2400M_BUS_RESET_RETRIES = 3,
};

/**
 * struct i2400m_poke_table - Hardware poke table for the Intel 2400m
 *
 * This structure will be used to create a device specific poke table
 * to put the device in a consistant state at boot time.
 *
 * @address: The device address to poke
 *
 * @data: The data value to poke to the device address
 *
 */
struct i2400m_poke_table{
	__le32 address;
	__le32 data;
};

#define I2400M_FW_POKE(a, d) {		\
	.address = cpu_to_le32(a),	\
	.data = cpu_to_le32(d)		\
}


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
struct i2400m_barker_db;

/**
 * struct i2400m - descriptor for an Intel 2400m
 *
 * Members marked with [fill] must be filled out/initialized before
 * calling i2400m_setup().
 *
 * Note the @bus_setup/@bus_release, @bus_dev_start/@bus_dev_release
 * call pairs are very much doing almost the same, and depending on
 * the underlying bus, some stuff has to be put in one or the
 * other. The idea of setup/release is that they setup the minimal
 * amount needed for loading firmware, where us dev_start/stop setup
 * the rest needed to do full data/control traffic.
 *
 * @bus_tx_block_size: [fill] SDIO imposes a 256 block size, USB 16,
 *     so we have a tx_blk_size variable that the bus layer sets to
 *     tell the engine how much of that we need.
 *
 * @bus_tx_room_min: [fill] Minimum room required while allocating
 *     TX queue's buffer space for message header. SDIO requires
 *     224 bytes and USB 16 bytes. Refer bus specific driver code
 *     for details.
 *
 * @bus_pl_size_max: [fill] Maximum payload size.
 *
 * @bus_setup: [optional fill] Function called by the bus-generic code
 *     [i2400m_setup()] to setup the basic bus-specific communications
 *     to the the device needed to load firmware. See LIFE CYCLE above.
 *
 *     NOTE: Doesn't need to upload the firmware, as that is taken
 *     care of by the bus-generic code.
 *
 * @bus_release: [optional fill] Function called by the bus-generic
 *     code [i2400m_release()] to shutdown the basic bus-specific
 *     communications to the the device needed to load firmware. See
 *     LIFE CYCLE above.
 *
 *     This function does not need to reset the device, just tear down
 *     all the host resources created to  handle communication with
 *     the device.
 *
 * @bus_dev_start: [optional fill] Function called by the bus-generic
 *     code [i2400m_dev_start()] to do things needed to start the
 *     device. See LIFE CYCLE above.
 *
 *     NOTE: Doesn't need to upload the firmware, as that is taken
 *     care of by the bus-generic code.
 *
 * @bus_dev_stop: [optional fill] Function called by the bus-generic
 *     code [i2400m_dev_stop()] to do things needed for stopping the
 *     device. See LIFE CYCLE above.
 *
 *     This function does not need to reset the device, just tear down
 *     all the host resources created to handle communication with
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
 *     IMPORTANT: don't call reset on RT_BUS with i2400m->init_mutex
 *     held, as the .pre/.post reset handlers will deadlock.
 *
 * @bus_bm_retries: [fill] How many times shall a firmware upload /
 *     device initialization be retried? Different models of the same
 *     device might need different values, hence it is set by the
 *     bus-specific driver. Note this value is used in two places,
 *     i2400m_fw_dnload() and __i2400m_dev_start(); they won't become
 *     multiplicative (__i2400m_dev_start() calling N times
 *     i2400m_fw_dnload() and this trying N times to download the
 *     firmware), as if __i2400m_dev_start() only retries if the
 *     firmware crashed while initializing the device (not in a
 *     general case).
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
 * @bus_bm_pokes_table: [fill/optional] A table of device addresses
 *     and values that will be poked at device init time to move the
 *     device to the correct state for the type of boot/firmware being
 *     used.  This table MUST be terminated with (0x000000,
 *     0x00000000) or bad things will happen.
 *
 *
 * @wimax_dev: WiMAX generic device for linkage into the kernel WiMAX
 *     stack. Due to the way a net_device is allocated, we need to
 *     force this to be the first field so that we can get from
 *     netdev_priv() the right pointer.
 *
 * @updown: the device is up and ready for transmitting control and
 *     data packets. This implies @ready (communication infrastructure
 *     with the device is ready) and the device's firmware has been
 *     loaded and the device initialized.
 *
 *     Write to it only inside a i2400m->init_mutex protected area
 *     followed with a wmb(); rmb() before accesing (unless locked
 *     inside i2400m->init_mutex). Read access can be loose like that
 *     [just using rmb()] because the paths that use this also do
 *     other error checks later on.
 *
 * @ready: Communication infrastructure with the device is ready, data
 *     frames can start to be passed around (this is lighter than
 *     using the WiMAX state for certain hot paths).
 *
 *     Write to it only inside a i2400m->init_mutex protected area
 *     followed with a wmb(); rmb() before accesing (unless locked
 *     inside i2400m->init_mutex). Read access can be loose like that
 *     [just using rmb()] because the paths that use this also do
 *     other error checks later on.
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
 * @rx_lock: spinlock to protect RX members and rx_roq_refcount.
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
 * @rx_roq_refcount: refcount rx_roq. This refcounts any access to
 *     rx_roq thus preventing rx_roq being destroyed when rx_roq
 *     is being accessed. rx_roq_refcount is protected by rx_lock.
 *
 * @rx_reports: reports received from the device that couldn't be
 *     processed because the driver wasn't still ready; when ready,
 *     they are pulled from here and chewed.
 *
 * @rx_reports_ws: Work struct used to kick a scan of the RX reports
 *     list and to process each.
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
 *
 * @fw_hdrs: NULL terminated array of pointers to the firmware
 *     headers. This is only available during firmware load time.
 *
 * @fw_cached: Used to cache firmware when the system goes to
 *     suspend/standby/hibernation (as on resume we can't read it). If
 *     NULL, no firmware was cached, read it. If ~0, you can't read
 *     any firmware files (the system still didn't come out of suspend
 *     and failed to cache one), so abort; otherwise, a valid cached
 *     firmware to be used. Access to this variable is protected by
 *     the spinlock i2400m->rx_lock.
 *
 * @barker: barker type that the device uses; this is initialized by
 *     i2400m_is_boot_barker() the first time it is called. Then it
 *     won't change during the life cycle of the device and everytime
 *     a boot barker is received, it is just verified for it being the
 *     same.
 *
 * @pm_notifier: used to register for PM events
 *
 * @bus_reset_retries: counter for the number of bus resets attempted for
 *	this boot. It's not for tracking the number of bus resets during
 *	the whole driver life cycle (from insmod to rmmod) but for the
 *	number of dev_start() executed until dev_start() returns a success
 *	(ie: a good boot means a dev_stop() followed by a successful
 *	dev_start()). dev_reset_handler() increments this counter whenever
 *	it is triggering a bus reset. It checks this counter to decide if a
 *	subsequent bus reset should be retried. dev_reset_handler() retries
 *	the bus reset until dev_start() succeeds or the counter reaches
 *	I2400M_BUS_RESET_RETRIES. The counter is cleared to 0 in
 *	dev_reset_handle() when dev_start() returns a success,
 *	ie: a successul boot is completed.
 *
 * @alive: flag to denote if the device *should* be alive. This flag is
 *	everything like @updown (see doc for @updown) except reflecting
 *	the device state *we expect* rather than the actual state as denoted
 *	by @updown. It is set 1 whenever @updown is set 1 in dev_start().
 *	Then the device is expected to be alive all the time
 *	(i2400m->alive remains 1) until the driver is removed. Therefore
 *	all the device reboot events detected can be still handled properly
 *	by either dev_reset_handle() or .pre_reset/.post_reset as long as
 *	the driver presents. It is set 0 along with @updown in dev_stop().
 *
 * @error_recovery: flag to denote if we are ready to take an error recovery.
 *	0 for ready to take an error recovery; 1 for not ready. It is
 *	initialized to 1 while probe() since we don't tend to take any error
 *	recovery during probe(). It is decremented by 1 whenever dev_start()
 *	succeeds to indicate we are ready to take error recovery from now on.
 *	It is checked every time we wanna schedule an error recovery. If an
 *	error recovery is already in place (error_recovery was set 1), we
 *	should not schedule another one until the last one is done.
 */
struct i2400m {
	struct wimax_dev wimax_dev;	/* FIRST! See doc */

	unsigned updown:1;		/* Network device is up or down */
	unsigned boot_mode:1;		/* is the device in boot mode? */
	unsigned sboot:1;		/* signed or unsigned fw boot */
	unsigned ready:1;		/* Device comm infrastructure ready */
	unsigned rx_reorder:1;		/* RX reorder is enabled */
	u8 trace_msg_from_user;		/* echo rx msgs to 'trace' pipe */
					/* typed u8 so /sys/kernel/debug/u8 can tweak */
	enum i2400m_system_state state;
	wait_queue_head_t state_wq;	/* Woken up when on state updates */

	size_t bus_tx_block_size;
	size_t bus_tx_room_min;
	size_t bus_pl_size_max;
	unsigned bus_bm_retries;

	int (*bus_setup)(struct i2400m *);
	int (*bus_dev_start)(struct i2400m *);
	void (*bus_dev_stop)(struct i2400m *);
	void (*bus_release)(struct i2400m *);
	void (*bus_tx_kick)(struct i2400m *);
	int (*bus_reset)(struct i2400m *, enum i2400m_reset_type);
	ssize_t (*bus_bm_cmd_send)(struct i2400m *,
				   const struct i2400m_bootrom_header *,
				   size_t, int flags);
	ssize_t (*bus_bm_wait_for_ack)(struct i2400m *,
				       struct i2400m_bootrom_header *, size_t);
	const char **bus_fw_names;
	unsigned bus_bm_mac_addr_impaired:1;
	const struct i2400m_poke_table *bus_bm_pokes_table;

	spinlock_t tx_lock;		/* protect TX state */
	void *tx_buf;
	size_t tx_in, tx_out;
	struct i2400m_msg_hdr *tx_msg;
	size_t tx_sequence, tx_msg_size;
	/* TX stats */
	unsigned tx_pl_num, tx_pl_max, tx_pl_min,
		tx_num, tx_size_acc, tx_size_min, tx_size_max;

	/* RX stuff */
	/* protect RX state and rx_roq_refcount */
	spinlock_t rx_lock;
	unsigned rx_pl_num, rx_pl_max, rx_pl_min,
		rx_num, rx_size_acc, rx_size_min, rx_size_max;
	struct i2400m_roq *rx_roq;	/* access is refcounted */
	struct kref rx_roq_refcount;	/* refcount access to rx_roq */
	u8 src_mac_addr[ETH_HLEN];
	struct list_head rx_reports;	/* under rx_lock! */
	struct work_struct rx_report_ws;

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
	const struct i2400m_bcf_hdr **fw_hdrs;
	struct i2400m_fw *fw_cached;	/* protected by rx_lock */
	struct i2400m_barker_db *barker;

	struct notifier_block pm_notifier;

	/* counting bus reset retries in this boot */
	atomic_t bus_reset_retries;

	/* if the device is expected to be alive */
	unsigned alive;

	/* 0 if we are ready for error recovery; 1 if not ready  */
	atomic_t error_recovery;

};


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
 *     rom after reading the MAC address. This is quite a dirty hack,
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
extern int i2400m_is_boot_barker(struct i2400m *, const void *, size_t);
static inline
int i2400m_is_d2h_barker(const void *buf)
{
	const __le32 *barker = buf;
	return le32_to_cpu(*barker) == I2400M_D2H_MSG_BARKER;
}
extern void i2400m_unknown_barker(struct i2400m *, const void *, size_t);

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
extern void i2400m_init(struct i2400m *);
extern int i2400m_reset(struct i2400m *, enum i2400m_reset_type);
extern void i2400m_netdev_setup(struct net_device *net_dev);
extern int i2400m_sysfs_setup(struct device_driver *);
extern void i2400m_sysfs_release(struct device_driver *);
extern int i2400m_tx_setup(struct i2400m *);
extern void i2400m_wake_tx_work(struct work_struct *);
extern void i2400m_tx_release(struct i2400m *);

extern int i2400m_rx_setup(struct i2400m *);
extern void i2400m_rx_release(struct i2400m *);

extern void i2400m_fw_cache(struct i2400m *);
extern void i2400m_fw_uncache(struct i2400m *);

extern void i2400m_net_rx(struct i2400m *, struct sk_buff *, unsigned,
			  const void *, int);
extern void i2400m_net_erx(struct i2400m *, struct sk_buff *,
			   enum i2400m_cs);
extern void i2400m_net_wake_stop(struct i2400m *);
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

/* Initialize/shutdown the device */
extern int i2400m_dev_initialize(struct i2400m *);
extern void i2400m_dev_shutdown(struct i2400m *);

extern struct attribute_group i2400m_dev_attr_group;


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

extern int i2400m_dev_reset_handle(struct i2400m *, const char *);
extern int i2400m_pre_reset(struct i2400m *);
extern int i2400m_post_reset(struct i2400m *);
extern void i2400m_error_recovery(struct i2400m *);

/*
 * _setup()/_release() are called by the probe/disconnect functions of
 * the bus-specific drivers.
 */
extern int i2400m_setup(struct i2400m *, enum i2400m_bri bm_flags);
extern void i2400m_release(struct i2400m *);

extern int i2400m_rx(struct i2400m *, struct sk_buff *);
extern struct i2400m_msg_hdr *i2400m_tx_msg_get(struct i2400m *, size_t *);
extern void i2400m_tx_msg_sent(struct i2400m *);


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
	size_t pl_size;
	u8 pl[0];
};

extern int i2400m_msg_check_status(const struct i2400m_l3l4_hdr *,
				   char *, size_t);
extern int i2400m_msg_size_check(struct i2400m *,
				 const struct i2400m_l3l4_hdr *, size_t);
extern struct sk_buff *i2400m_msg_to_dev(struct i2400m *, const void *, size_t);
extern void i2400m_msg_to_dev_cancel_wait(struct i2400m *, int);
extern void i2400m_report_hook(struct i2400m *,
			       const struct i2400m_l3l4_hdr *, size_t);
extern void i2400m_report_hook_work(struct work_struct *);
extern int i2400m_cmd_enter_powersave(struct i2400m *);
extern int i2400m_cmd_exit_idle(struct i2400m *);
extern struct sk_buff *i2400m_get_device_info(struct i2400m *);
extern int i2400m_firmware_check(struct i2400m *);
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


/* module initialization helpers */
extern int i2400m_barker_db_init(const char *);
extern void i2400m_barker_db_exit(void);



#endif /* #ifndef __I2400M_H__ */
