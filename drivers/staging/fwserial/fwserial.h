#ifndef _FIREWIRE_FWSERIAL_H
#define _FIREWIRE_FWSERIAL_H

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/list.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include "dma_fifo.h"

#ifdef FWTTY_PROFILING
#define DISTRIBUTION_MAX_SIZE     8192
#define DISTRIBUTION_MAX_INDEX    (ilog2(DISTRIBUTION_MAX_SIZE) + 1)
static inline void fwtty_profile_data(unsigned stat[], unsigned val)
{
	int n = (val) ? min(ilog2(val) + 1, DISTRIBUTION_MAX_INDEX) : 0;
	++stat[n];
}
#else
#define DISTRIBUTION_MAX_INDEX    0
#define fwtty_profile_data(st, n)
#endif

/* Parameters for both VIRT_CABLE_PLUG & VIRT_CABLE_PLUG_RSP mgmt codes */
struct virt_plug_params {
	__be32  status_hi;
	__be32  status_lo;
	__be32	fifo_hi;
	__be32	fifo_lo;
	__be32	fifo_len;
};

struct peer_work_params {
	union {
		struct virt_plug_params plug_req;
	};
};

/**
 * fwtty_peer: structure representing local & remote unit devices
 * @unit: unit child device of fw_device node
 * @serial: back pointer to associated fw_serial aggregate
 * @guid: unique 64-bit guid for this unit device
 * @generation: most recent bus generation
 * @node_id: most recent node_id
 * @speed: link speed of peer (0 = S100, 2 = S400, ... 5 = S3200)
 * @mgmt_addr: bus addr region to write mgmt packets to
 * @status_addr: bus addr register to write line status to
 * @fifo_addr: bus addr region to write serial output to
 * @fifo_len:  max length for single write to fifo_addr
 * @list: link for insertion into fw_serial's peer_list
 * @rcu: for deferring peer reclamation
 * @lock: spinlock to synchonize changes to state & port fields
 * @work: only one work item can be queued at any one time
 *        Note: pending work is canceled prior to removal, so this
 *        peer is valid for at least the lifetime of the work function
 * @work_params: parameter block for work functions
 * @timer: timer for resetting peer state if remote request times out
 * @state: current state
 * @connect: work item for auto-connecting
 * @connect_retries: # of connections already attempted
 * @port: associated tty_port (usable if state == FWSC_ATTACHED)
 */
struct fwtty_peer {
	struct fw_unit		*unit;
	struct fw_serial	*serial;
	u64			guid;
	int			generation;
	int			node_id;
	unsigned		speed;
	int			max_payload;
	u64			mgmt_addr;

	/* these are usable only if state == FWSC_ATTACHED */
	u64			status_addr;
	u64			fifo_addr;
	int			fifo_len;

	struct list_head	list;
	struct rcu_head		rcu;

	spinlock_t		lock;
	work_func_t		workfn;
	struct work_struct	work;
	struct peer_work_params work_params;
	struct timer_list	timer;
	int			state;
	struct delayed_work	connect;
	int			connect_retries;

	struct fwtty_port	*port;
};

#define to_peer(ptr, field)	(container_of(ptr, struct fwtty_peer, field))

/* state values for fwtty_peer.state field */
enum fwtty_peer_state {
	FWPS_GONE,
	FWPS_NOT_ATTACHED,
	FWPS_ATTACHED,
	FWPS_PLUG_PENDING,
	FWPS_PLUG_RESPONDING,
	FWPS_UNPLUG_PENDING,
	FWPS_UNPLUG_RESPONDING,

	FWPS_NO_MGMT_ADDR = -1,
};

#define CONNECT_RETRY_DELAY	HZ
#define MAX_CONNECT_RETRIES	10

/* must be holding peer lock for these state funclets */
static inline void peer_set_state(struct fwtty_peer *peer, int new)
{
	peer->state = new;
}

static inline struct fwtty_port *peer_revert_state(struct fwtty_peer *peer)
{
	struct fwtty_port *port = peer->port;

	peer->port = NULL;
	peer_set_state(peer, FWPS_NOT_ATTACHED);
	return port;
}

struct fwserial_mgmt_pkt {
	struct {
		__be16		len;
		__be16		code;
	} hdr;
	union {
		struct virt_plug_params plug_req;
		struct virt_plug_params plug_rsp;
	};
} __packed;

/* fwserial_mgmt_packet codes */
#define FWSC_RSP_OK			0x0000
#define FWSC_RSP_NACK			0x8000
#define FWSC_CODE_MASK			0x0fff

#define FWSC_VIRT_CABLE_PLUG		1
#define FWSC_VIRT_CABLE_UNPLUG		2
#define FWSC_VIRT_CABLE_PLUG_RSP	3
#define FWSC_VIRT_CABLE_UNPLUG_RSP	4

/* 1 min. plug timeout -- suitable for userland authorization */
#define VIRT_CABLE_PLUG_TIMEOUT		(60 * HZ)

struct stats {
	unsigned	xchars;
	unsigned	dropped;
	unsigned	tx_stall;
	unsigned	fifo_errs;
	unsigned	sent;
	unsigned	lost;
	unsigned	throttled;
	unsigned	reads[DISTRIBUTION_MAX_INDEX + 1];
	unsigned	writes[DISTRIBUTION_MAX_INDEX + 1];
	unsigned	txns[DISTRIBUTION_MAX_INDEX + 1];
	unsigned	unthrottle[DISTRIBUTION_MAX_INDEX + 1];
};

struct fwconsole_ops {
	void (*notify)(int code, void *data);
	void (*stats)(struct stats *stats, void *data);
	void (*proc_show)(struct seq_file *m, void *data);
};

/* codes for console ops notify */
#define FWCON_NOTIFY_ATTACH		1
#define FWCON_NOTIFY_DETACH		2

/**
 * fwtty_port: structure used to track/represent underlying tty_port
 * @port: underlying tty_port
 * @device: tty device
 * @index: index into port_table for this particular port
 *    note: minor = index + minor_start assigned by tty_alloc_driver()
 * @serial: back pointer to the containing fw_serial
 * @rx_handler: bus address handler for unique addr region used by remotes
 *              to communicate with this port. Every port uses
 *		fwtty_port_handler() for per port transactions.
 * @fwcon_ops: ops for attached fw_console (if any)
 * @con_data: private data for fw_console
 * @wait_tx: waitqueue for sleeping until writer/drain completes tx
 * @emit_breaks: delayed work responsible for generating breaks when the
 *               break line status is active
 * @cps : characters per second computed from the termios settings
 * @break_last: timestamp in jiffies from last emit_breaks
 * @hangup: work responsible for HUPing when carrier is dropped/lost
 * @mstatus: loose virtualization of LSR/MSR
 *         bits 15..0  correspond to TIOCM_* bits
 *         bits 19..16 reserved for mctrl
 *         bit 20      OOB_TX_THROTTLE
 *	   bits 23..21 reserved
 *         bits 31..24 correspond to UART_LSR_* bits
 * @lock: spinlock for protecting concurrent access to fields below it
 * @mctrl: loose virtualization of MCR
 *         bits 15..0  correspond to TIOCM_* bits
 *         bit 16      OOB_RX_THROTTLE
 *         bits 19..17 reserved
 *	   bits 31..20 reserved for mstatus
 * @drain: delayed work scheduled to ensure that writes are flushed.
 *         The work can race with the writer but concurrent sending is
 *         prevented with the IN_TX flag. Scheduled under lock to
 *         limit scheduling when fifo has just been drained.
 * @tx_fifo: fifo used to store & block-up writes for dma to remote
 * @max_payload: max bytes transmissible per dma (based on peer's max_payload)
 * @status_mask: UART_LSR_* bitmask significant to rx (based on termios)
 * @ignore_mask: UART_LSR_* bitmask of states to ignore (also based on termios)
 * @break_ctl: if set, port is 'sending break' to remote
 * @write_only: self-explanatory
 * @overrun: previous rx was lost (partially or completely)
 * @loopback: if set, port is in loopback mode
 * @flags: atomic bit flags
 *         bit 0: IN_TX - gate to allow only one cpu to send from the dma fifo
 *                        at a time.
 *         bit 1: STOP_TX - force tx to exit while sending
 * @peer: rcu-pointer to associated fwtty_peer (if attached)
 *        NULL if no peer attached
 * @icount: predefined statistics reported by the TIOCGICOUNT ioctl
 * @stats: additional statistics reported in /proc/tty/driver/firewire_serial
 */
struct fwtty_port {
	struct tty_port		   port;
	struct device		   *device;
	unsigned		   index;
	struct fw_serial	   *serial;
	struct fw_address_handler  rx_handler;

	struct fwconsole_ops	   *fwcon_ops;
	void			   *con_data;

	wait_queue_head_t	   wait_tx;
	struct delayed_work	   emit_breaks;
	unsigned		   cps;
	unsigned long		   break_last;

	struct work_struct	   hangup;

	unsigned		   mstatus;

	spinlock_t		   lock;
	unsigned		   mctrl;
	struct delayed_work	   drain;
	struct dma_fifo		   tx_fifo;
	int			   max_payload;
	unsigned		   status_mask;
	unsigned		   ignore_mask;
	unsigned		   break_ctl:1,
				   write_only:1,
				   overrun:1,
				   loopback:1;
	unsigned long		   flags;

	struct fwtty_peer __rcu	   *peer;

	struct async_icount	   icount;
	struct stats		   stats;
};

#define to_port(ptr, field)	(container_of(ptr, struct fwtty_port, field))

/* bit #s for flags field */
#define IN_TX                      0
#define STOP_TX                    1

/* bitmasks for special mctrl/mstatus bits */
#define OOB_RX_THROTTLE   0x00010000
#define MCTRL_RSRVD       0x000e0000
#define OOB_TX_THROTTLE   0x00100000
#define MSTATUS_RSRVD     0x00e00000

#define MCTRL_MASK        (TIOCM_DTR | TIOCM_RTS | TIOCM_OUT1 | TIOCM_OUT2 | \
			   TIOCM_LOOP | OOB_RX_THROTTLE | MCTRL_RSRVD)

/* XXX even every 1/50th secs. may be unnecessarily accurate */
/* delay in jiffies between brk emits */
#define FREQ_BREAKS        (HZ / 50)

/* Ports are allocated in blocks of num_ports for each fw_card */
#define MAX_CARD_PORTS           CONFIG_FWTTY_MAX_CARD_PORTS
#define MAX_TOTAL_PORTS          CONFIG_FWTTY_MAX_TOTAL_PORTS

/* tuning parameters */
#define FWTTY_PORT_TXFIFO_LEN	4096
#define FWTTY_PORT_MAX_PEND_DMA    8    /* costs a cache line per pend */
#define DRAIN_THRESHOLD         1024
#define MAX_ASYNC_PAYLOAD       4096    /* ohci-defined limit          */
#define WRITER_MINIMUM           128
/* TODO: how to set watermark to AR context size? see fwtty_rx() */
#define HIGH_WATERMARK         32768	/* AR context is 32K	       */

/*
 * Size of bus addr region above 4GB used per port as the recv addr
 * - must be at least as big as the MAX_ASYNC_PAYLOAD
 */
#define FWTTY_PORT_RXFIFO_LEN	MAX_ASYNC_PAYLOAD

/**
 * fw_serial: aggregate used to associate tty ports with specific fw_card
 * @card: fw_card associated with this fw_serial device (1:1 association)
 * @kref: reference-counted multi-port management allows delayed destroy
 * @self: local unit device as 'peer'. Not valid until local unit device
 *         is enumerated.
 * @list: link for insertion into fwserial_list
 * @peer_list: list of local & remote unit devices attached to this card
 * @ports: fixed array of tty_ports provided by this serial device
 */
struct fw_serial {
	struct fw_card	  *card;
	struct kref	  kref;

	struct dentry	  *debugfs;
	struct fwtty_peer *self;

	struct list_head  list;
	struct list_head  peer_list;

	struct fwtty_port *ports[MAX_CARD_PORTS];
};

#define to_serial(ptr, field)	(container_of(ptr, struct fw_serial, field))

#define TTY_DEV_NAME		    "fwtty"	/* ttyFW was taken           */
static const char tty_dev_name[] =  TTY_DEV_NAME;
static const char loop_dev_name[] = "fwloop";

extern struct tty_driver *fwtty_driver;

/*
 * Returns the max send async payload size in bytes based on the unit device
 * link speed. Self-limiting asynchronous bandwidth (via reducing the payload)
 * is not necessary and does not work, because
 *   1) asynchronous traffic will absorb all available bandwidth (less that
 *	being used for isochronous traffic)
 *   2) isochronous arbitration always wins.
 */
static inline int link_speed_to_max_payload(unsigned speed)
{
	/* Max async payload is 4096 - see IEEE 1394-2008 tables 6-4, 16-18 */
	return min(512 << speed, 4096);
}

#endif /* _FIREWIRE_FWSERIAL_H */
