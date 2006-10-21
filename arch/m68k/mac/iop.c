/*
 * I/O Processor (IOP) management
 * Written and (C) 1999 by Joshua M. Thompson (funaho@jurai.org)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice and this list of conditions.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice and this list of conditions in the documentation and/or other
 *    materials provided with the distribution.
 */

/*
 * The IOP chips are used in the IIfx and some Quadras (900, 950) to manage
 * serial and ADB. They are actually a 6502 processor and some glue logic.
 *
 * 990429 (jmt) - Initial implementation, just enough to knock the SCC IOP
 *		  into compatible mode so nobody has to fiddle with the
 *		  Serial Switch control panel anymore.
 * 990603 (jmt) - Added code to grab the correct ISM IOP interrupt for OSS
 *		  and non-OSS machines (at least I hope it's correct on a
 *		  non-OSS machine -- someone with a Q900 or Q950 needs to
 *		  check this.)
 * 990605 (jmt) - Rearranged things a bit wrt IOP detection; iop_present is
 *		  gone, IOP base addresses are now in an array and the
 *		  globally-visible functions take an IOP number instead of an
 *		  an actual base address.
 * 990610 (jmt) - Finished the message passing framework and it seems to work.
 *		  Sending _definitely_ works; my adb-bus.c mods can send
 *		  messages and receive the MSG_COMPLETED status back from the
 *		  IOP. The trick now is figuring out the message formats.
 * 990611 (jmt) - More cleanups. Fixed problem where unclaimed messages on a
 *		  receive channel were never properly acknowledged. Bracketed
 *		  the remaining debug printk's with #ifdef's and disabled
 *		  debugging. I can now type on the console.
 * 990612 (jmt) - Copyright notice added. Reworked the way replies are handled.
 *		  It turns out that replies are placed back in the send buffer
 *		  for that channel; messages on the receive channels are always
 *		  unsolicited messages from the IOP (and our replies to them
 *		  should go back in the receive channel.) Also added tracking
 *		  of device names to the listener functions ala the interrupt
 *		  handlers.
 * 990729 (jmt) - Added passing of pt_regs structure to IOP handlers. This is
 *		  used by the new unified ADB driver.
 *
 * TODO:
 *
 * o Something should be periodically checking iop_alive() to make sure the
 *   IOP hasn't died.
 * o Some of the IOP manager routines need better error checking and
 *   return codes. Nothing major, just prettying up.
 */

/*
 * -----------------------
 * IOP Message Passing 101
 * -----------------------
 *
 * The host talks to the IOPs using a rather simple message-passing scheme via
 * a shared memory area in the IOP RAM. Each IOP has seven "channels"; each
 * channel is conneced to a specific software driver on the IOP. For example
 * on the SCC IOP there is one channel for each serial port. Each channel has
 * an incoming and and outgoing message queue with a depth of one.
 *
 * A message is 32 bytes plus a state byte for the channel (MSG_IDLE, MSG_NEW,
 * MSG_RCVD, MSG_COMPLETE). To send a message you copy the message into the
 * buffer, set the state to MSG_NEW and signal the IOP by setting the IRQ flag
 * in the IOP control to 1. The IOP will move the state to MSG_RCVD when it
 * receives the message and then to MSG_COMPLETE when the message processing
 * has completed. It is the host's responsibility at that point to read the
 * reply back out of the send channel buffer and reset the channel state back
 * to MSG_IDLE.
 *
 * To receive message from the IOP the same procedure is used except the roles
 * are reversed. That is, the IOP puts message in the channel with a state of
 * MSG_NEW, and the host receives the message and move its state to MSG_RCVD
 * and then to MSG_COMPLETE when processing is completed and the reply (if any)
 * has been placed back in the receive channel. The IOP will then reset the
 * channel state to MSG_IDLE.
 *
 * Two sets of host interrupts are provided, INT0 and INT1. Both appear on one
 * interrupt level; they are distinguished by a pair of bits in the IOP status
 * register. The IOP will raise INT0 when one or more messages in the send
 * channels have gone to the MSG_COMPLETE state and it will raise INT1 when one
 * or more messages on the receive channels have gone to the MSG_NEW state.
 *
 * Since each channel handles only one message we have to implement a small
 * interrupt-driven queue on our end. Messages to be sent are placed on the
 * queue for sending and contain a pointer to an optional callback function.
 * The handler for a message is called when the message state goes to
 * MSG_COMPLETE.
 *
 * For receiving message we maintain a list of handler functions to call when
 * a message is received on that IOP/channel combination. The handlers are
 * called much like an interrupt handler and are passed a copy of the message
 * from the IOP. The message state will be in MSG_RCVD while the handler runs;
 * it is the handler's responsibility to call iop_complete_message() when
 * finished; this function moves the message state to MSG_COMPLETE and signals
 * the IOP. This two-step process is provided to allow the handler to defer
 * message processing to a bottom-half handler if the processing will take
 * a signifigant amount of time (handlers are called at interrupt time so they
 * should execute quickly.)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>

#include <asm/bootinfo.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_iop.h>
#include <asm/mac_oss.h>

/*#define DEBUG_IOP*/

/* Set to nonezero if the IOPs are present. Set by iop_init() */

int iop_scc_present,iop_ism_present;

#ifdef CONFIG_PROC_FS
static int iop_get_proc_info(char *, char **, off_t, int);
#endif /* CONFIG_PROC_FS */

/* structure for tracking channel listeners */

struct listener {
	const char *devname;
	void (*handler)(struct iop_msg *);
};

/*
 * IOP structures for the two IOPs
 *
 * The SCC IOP controls both serial ports (A and B) as its two functions.
 * The ISM IOP controls the SWIM (floppy drive) and ADB.
 */

static volatile struct mac_iop *iop_base[NUM_IOPS];

/*
 * IOP message queues
 */

static struct iop_msg iop_msg_pool[NUM_IOP_MSGS];
static struct iop_msg *iop_send_queue[NUM_IOPS][NUM_IOP_CHAN];
static struct listener iop_listeners[NUM_IOPS][NUM_IOP_CHAN];

irqreturn_t iop_ism_irq(int, void *);

extern void oss_irq_enable(int);

/*
 * Private access functions
 */

static __inline__ void iop_loadaddr(volatile struct mac_iop *iop, __u16 addr)
{
	iop->ram_addr_lo = addr;
	iop->ram_addr_hi = addr >> 8;
}

static __inline__ __u8 iop_readb(volatile struct mac_iop *iop, __u16 addr)
{
	iop->ram_addr_lo = addr;
	iop->ram_addr_hi = addr >> 8;
	return iop->ram_data;
}

static __inline__ void iop_writeb(volatile struct mac_iop *iop, __u16 addr, __u8 data)
{
	iop->ram_addr_lo = addr;
	iop->ram_addr_hi = addr >> 8;
	iop->ram_data = data;
}

static __inline__ void iop_stop(volatile struct mac_iop *iop)
{
	iop->status_ctrl &= ~IOP_RUN;
}

static __inline__ void iop_start(volatile struct mac_iop *iop)
{
	iop->status_ctrl = IOP_RUN | IOP_AUTOINC;
}

static __inline__ void iop_bypass(volatile struct mac_iop *iop)
{
	iop->status_ctrl |= IOP_BYPASS;
}

static __inline__ void iop_interrupt(volatile struct mac_iop *iop)
{
	iop->status_ctrl |= IOP_IRQ;
}

static int iop_alive(volatile struct mac_iop *iop)
{
	int retval;

	retval = (iop_readb(iop, IOP_ADDR_ALIVE) == 0xFF);
	iop_writeb(iop, IOP_ADDR_ALIVE, 0);
	return retval;
}

static struct iop_msg *iop_alloc_msg(void)
{
	int i;
	unsigned long flags;

	local_irq_save(flags);

	for (i = 0 ; i < NUM_IOP_MSGS ; i++) {
		if (iop_msg_pool[i].status == IOP_MSGSTATUS_UNUSED) {
			iop_msg_pool[i].status = IOP_MSGSTATUS_WAITING;
			local_irq_restore(flags);
			return &iop_msg_pool[i];
		}
	}

	local_irq_restore(flags);
	return NULL;
}

static void iop_free_msg(struct iop_msg *msg)
{
	msg->status = IOP_MSGSTATUS_UNUSED;
}

/*
 * This is called by the startup code before anything else. Its purpose
 * is to find and initialize the IOPs early in the boot sequence, so that
 * the serial IOP can be placed into bypass mode _before_ we try to
 * initialize the serial console.
 */

void __init iop_preinit(void)
{
	if (macintosh_config->scc_type == MAC_SCC_IOP) {
		if (macintosh_config->ident == MAC_MODEL_IIFX) {
			iop_base[IOP_NUM_SCC] = (struct mac_iop *) SCC_IOP_BASE_IIFX;
		} else {
			iop_base[IOP_NUM_SCC] = (struct mac_iop *) SCC_IOP_BASE_QUADRA;
		}
		iop_base[IOP_NUM_SCC]->status_ctrl = 0x87;
		iop_scc_present = 1;
	} else {
		iop_base[IOP_NUM_SCC] = NULL;
		iop_scc_present = 0;
	}
	if (macintosh_config->adb_type == MAC_ADB_IOP) {
		if (macintosh_config->ident == MAC_MODEL_IIFX) {
			iop_base[IOP_NUM_ISM] = (struct mac_iop *) ISM_IOP_BASE_IIFX;
		} else {
			iop_base[IOP_NUM_ISM] = (struct mac_iop *) ISM_IOP_BASE_QUADRA;
		}
		iop_base[IOP_NUM_ISM]->status_ctrl = 0;
		iop_ism_present = 1;
	} else {
		iop_base[IOP_NUM_ISM] = NULL;
		iop_ism_present = 0;
	}
}

/*
 * Initialize the IOPs, if present.
 */

void __init iop_init(void)
{
	int i;

	if (iop_scc_present) {
		printk("IOP: detected SCC IOP at %p\n", iop_base[IOP_NUM_SCC]);
	}
	if (iop_ism_present) {
		printk("IOP: detected ISM IOP at %p\n", iop_base[IOP_NUM_ISM]);
		iop_start(iop_base[IOP_NUM_ISM]);
		iop_alive(iop_base[IOP_NUM_ISM]); /* clears the alive flag */
	}

	/* Make the whole pool available and empty the queues */

	for (i = 0 ; i < NUM_IOP_MSGS ; i++) {
		iop_msg_pool[i].status = IOP_MSGSTATUS_UNUSED;
	}

	for (i = 0 ; i < NUM_IOP_CHAN ; i++) {
		iop_send_queue[IOP_NUM_SCC][i] = NULL;
		iop_send_queue[IOP_NUM_ISM][i] = NULL;
		iop_listeners[IOP_NUM_SCC][i].devname = NULL;
		iop_listeners[IOP_NUM_SCC][i].handler = NULL;
		iop_listeners[IOP_NUM_ISM][i].devname = NULL;
		iop_listeners[IOP_NUM_ISM][i].handler = NULL;
	}

#if 0	/* Crashing in 2.4 now, not yet sure why.   --jmt */
#ifdef CONFIG_PROC_FS
	create_proc_info_entry("mac_iop", 0, &proc_root, iop_get_proc_info);
#endif
#endif
}

/*
 * Register the interrupt handler for the IOPs.
 * TODO: might be wrong for non-OSS machines. Anyone?
 */

void __init iop_register_interrupts(void)
{
	if (iop_ism_present) {
		if (oss_present) {
			request_irq(OSS_IRQLEV_IOPISM, iop_ism_irq,
					IRQ_FLG_LOCK, "ISM IOP",
					(void *) IOP_NUM_ISM);
			oss_irq_enable(IRQ_MAC_ADB);
		} else {
			request_irq(IRQ_VIA2_0, iop_ism_irq,
					IRQ_FLG_LOCK|IRQ_FLG_FAST, "ISM IOP",
					(void *) IOP_NUM_ISM);
		}
		if (!iop_alive(iop_base[IOP_NUM_ISM])) {
			printk("IOP: oh my god, they killed the ISM IOP!\n");
		} else {
			printk("IOP: the ISM IOP seems to be alive.\n");
		}
	}
}

/*
 * Register or unregister a listener for a specific IOP and channel
 *
 * If the handler pointer is NULL the current listener (if any) is
 * unregistered. Otherwise the new listener is registered provided
 * there is no existing listener registered.
 */

int iop_listen(uint iop_num, uint chan,
		void (*handler)(struct iop_msg *),
		const char *devname)
{
	if ((iop_num >= NUM_IOPS) || !iop_base[iop_num]) return -EINVAL;
	if (chan >= NUM_IOP_CHAN) return -EINVAL;
	if (iop_listeners[iop_num][chan].handler && handler) return -EINVAL;
	iop_listeners[iop_num][chan].devname = devname;
	iop_listeners[iop_num][chan].handler = handler;
	return 0;
}

/*
 * Complete reception of a message, which just means copying the reply
 * into the buffer, setting the channel state to MSG_COMPLETE and
 * notifying the IOP.
 */

void iop_complete_message(struct iop_msg *msg)
{
	int iop_num = msg->iop_num;
	int chan = msg->channel;
	int i,offset;

#ifdef DEBUG_IOP
	printk("iop_complete(%p): iop %d chan %d\n", msg, msg->iop_num, msg->channel);
#endif

	offset = IOP_ADDR_RECV_MSG + (msg->channel * IOP_MSG_LEN);

	for (i = 0 ; i < IOP_MSG_LEN ; i++, offset++) {
		iop_writeb(iop_base[iop_num], offset, msg->reply[i]);
	}

	iop_writeb(iop_base[iop_num],
		   IOP_ADDR_RECV_STATE + chan, IOP_MSG_COMPLETE);
	iop_interrupt(iop_base[msg->iop_num]);

	iop_free_msg(msg);
}

/*
 * Actually put a message into a send channel buffer
 */

static void iop_do_send(struct iop_msg *msg)
{
	volatile struct mac_iop *iop = iop_base[msg->iop_num];
	int i,offset;

	offset = IOP_ADDR_SEND_MSG + (msg->channel * IOP_MSG_LEN);

	for (i = 0 ; i < IOP_MSG_LEN ; i++, offset++) {
		iop_writeb(iop, offset, msg->message[i]);
	}

	iop_writeb(iop, IOP_ADDR_SEND_STATE + msg->channel, IOP_MSG_NEW);

	iop_interrupt(iop);
}

/*
 * Handle sending a message on a channel that
 * has gone into the IOP_MSG_COMPLETE state.
 */

static void iop_handle_send(uint iop_num, uint chan)
{
	volatile struct mac_iop *iop = iop_base[iop_num];
	struct iop_msg *msg,*msg2;
	int i,offset;

#ifdef DEBUG_IOP
	printk("iop_handle_send: iop %d channel %d\n", iop_num, chan);
#endif

	iop_writeb(iop, IOP_ADDR_SEND_STATE + chan, IOP_MSG_IDLE);

	if (!(msg = iop_send_queue[iop_num][chan])) return;

	msg->status = IOP_MSGSTATUS_COMPLETE;
	offset = IOP_ADDR_SEND_MSG + (chan * IOP_MSG_LEN);
	for (i = 0 ; i < IOP_MSG_LEN ; i++, offset++) {
		msg->reply[i] = iop_readb(iop, offset);
	}
	if (msg->handler) (*msg->handler)(msg);
	msg2 = msg;
	msg = msg->next;
	iop_free_msg(msg2);

	iop_send_queue[iop_num][chan] = msg;
	if (msg) iop_do_send(msg);
}

/*
 * Handle reception of a message on a channel that has
 * gone into the IOP_MSG_NEW state.
 */

static void iop_handle_recv(uint iop_num, uint chan)
{
	volatile struct mac_iop *iop = iop_base[iop_num];
	int i,offset;
	struct iop_msg *msg;

#ifdef DEBUG_IOP
	printk("iop_handle_recv: iop %d channel %d\n", iop_num, chan);
#endif

	msg = iop_alloc_msg();
	msg->iop_num = iop_num;
	msg->channel = chan;
	msg->status = IOP_MSGSTATUS_UNSOL;
	msg->handler = iop_listeners[iop_num][chan].handler;

	offset = IOP_ADDR_RECV_MSG + (chan * IOP_MSG_LEN);

	for (i = 0 ; i < IOP_MSG_LEN ; i++, offset++) {
		msg->message[i] = iop_readb(iop, offset);
	}

	iop_writeb(iop, IOP_ADDR_RECV_STATE + chan, IOP_MSG_RCVD);

	/* If there is a listener, call it now. Otherwise complete */
	/* the message ourselves to avoid possible stalls.         */

	if (msg->handler) {
		(*msg->handler)(msg);
	} else {
#ifdef DEBUG_IOP
		printk("iop_handle_recv: unclaimed message on iop %d channel %d\n", iop_num, chan);
		printk("iop_handle_recv:");
		for (i = 0 ; i < IOP_MSG_LEN ; i++) {
			printk(" %02X", (uint) msg->message[i]);
		}
		printk("\n");
#endif
		iop_complete_message(msg);
	}
}

/*
 * Send a message
 *
 * The message is placed at the end of the send queue. Afterwards if the
 * channel is idle we force an immediate send of the next message in the
 * queue.
 */

int iop_send_message(uint iop_num, uint chan, void *privdata,
		      uint msg_len, __u8 *msg_data,
		      void (*handler)(struct iop_msg *))
{
	struct iop_msg *msg, *q;

	if ((iop_num >= NUM_IOPS) || !iop_base[iop_num]) return -EINVAL;
	if (chan >= NUM_IOP_CHAN) return -EINVAL;
	if (msg_len > IOP_MSG_LEN) return -EINVAL;

	msg = iop_alloc_msg();
	if (!msg) return -ENOMEM;

	msg->next = NULL;
	msg->status = IOP_MSGSTATUS_WAITING;
	msg->iop_num = iop_num;
	msg->channel = chan;
	msg->caller_priv = privdata;
	memcpy(msg->message, msg_data, msg_len);
	msg->handler = handler;

	if (!(q = iop_send_queue[iop_num][chan])) {
		iop_send_queue[iop_num][chan] = msg;
	} else {
		while (q->next) q = q->next;
		q->next = msg;
	}

	if (iop_readb(iop_base[iop_num],
	    IOP_ADDR_SEND_STATE + chan) == IOP_MSG_IDLE) {
		iop_do_send(msg);
	}

	return 0;
}

/*
 * Upload code to the shared RAM of an IOP.
 */

void iop_upload_code(uint iop_num, __u8 *code_start,
		     uint code_len, __u16 shared_ram_start)
{
	if ((iop_num >= NUM_IOPS) || !iop_base[iop_num]) return;

	iop_loadaddr(iop_base[iop_num], shared_ram_start);

	while (code_len--) {
		iop_base[iop_num]->ram_data = *code_start++;
	}
}

/*
 * Download code from the shared RAM of an IOP.
 */

void iop_download_code(uint iop_num, __u8 *code_start,
		       uint code_len, __u16 shared_ram_start)
{
	if ((iop_num >= NUM_IOPS) || !iop_base[iop_num]) return;

	iop_loadaddr(iop_base[iop_num], shared_ram_start);

	while (code_len--) {
		*code_start++ = iop_base[iop_num]->ram_data;
	}
}

/*
 * Compare the code in the shared RAM of an IOP with a copy in system memory
 * and return 0 on match or the first nonmatching system memory address on
 * failure.
 */

__u8 *iop_compare_code(uint iop_num, __u8 *code_start,
		       uint code_len, __u16 shared_ram_start)
{
	if ((iop_num >= NUM_IOPS) || !iop_base[iop_num]) return code_start;

	iop_loadaddr(iop_base[iop_num], shared_ram_start);

	while (code_len--) {
		if (*code_start != iop_base[iop_num]->ram_data) {
			return code_start;
		}
		code_start++;
	}
	return (__u8 *) 0;
}

/*
 * Handle an ISM IOP interrupt
 */

irqreturn_t iop_ism_irq(int irq, void *dev_id)
{
	uint iop_num = (uint) dev_id;
	volatile struct mac_iop *iop = iop_base[iop_num];
	int i,state;

#ifdef DEBUG_IOP
	printk("iop_ism_irq: status = %02X\n", (uint) iop->status_ctrl);
#endif

	/* INT0 indicates a state change on an outgoing message channel */

	if (iop->status_ctrl & IOP_INT0) {
		iop->status_ctrl = IOP_INT0 | IOP_RUN | IOP_AUTOINC;
#ifdef DEBUG_IOP
		printk("iop_ism_irq: new status = %02X, send states",
			(uint) iop->status_ctrl);
#endif
		for (i = 0 ; i < NUM_IOP_CHAN  ; i++) {
			state = iop_readb(iop, IOP_ADDR_SEND_STATE + i);
#ifdef DEBUG_IOP
			printk(" %02X", state);
#endif
			if (state == IOP_MSG_COMPLETE) {
				iop_handle_send(iop_num, i);
			}
		}
#ifdef DEBUG_IOP
		printk("\n");
#endif
	}

	if (iop->status_ctrl & IOP_INT1) {	/* INT1 for incoming msgs */
		iop->status_ctrl = IOP_INT1 | IOP_RUN | IOP_AUTOINC;
#ifdef DEBUG_IOP
		printk("iop_ism_irq: new status = %02X, recv states",
			(uint) iop->status_ctrl);
#endif
		for (i = 0 ; i < NUM_IOP_CHAN ; i++) {
			state = iop_readb(iop, IOP_ADDR_RECV_STATE + i);
#ifdef DEBUG_IOP
			printk(" %02X", state);
#endif
			if (state == IOP_MSG_NEW) {
				iop_handle_recv(iop_num, i);
			}
		}
#ifdef DEBUG_IOP
		printk("\n");
#endif
	}
	return IRQ_HANDLED;
}

#ifdef CONFIG_PROC_FS

char *iop_chan_state(int state)
{
	switch(state) {
		case IOP_MSG_IDLE	: return "idle      ";
		case IOP_MSG_NEW	: return "new       ";
		case IOP_MSG_RCVD	: return "received  ";
		case IOP_MSG_COMPLETE	: return "completed ";
		default			: return "unknown   ";
	}
}

int iop_dump_one_iop(char *buf, int iop_num, char *iop_name)
{
	int i,len = 0;
	volatile struct mac_iop *iop = iop_base[iop_num];

	len += sprintf(buf+len, "%s IOP channel states:\n\n", iop_name);
	len += sprintf(buf+len, "##  send_state  recv_state  device\n");
	len += sprintf(buf+len, "------------------------------------------------\n");
	for (i = 0 ; i < NUM_IOP_CHAN ; i++) {
		len += sprintf(buf+len, "%2d  %10s  %10s  %s\n", i,
			iop_chan_state(iop_readb(iop, IOP_ADDR_SEND_STATE+i)),
			iop_chan_state(iop_readb(iop, IOP_ADDR_RECV_STATE+i)),
			iop_listeners[iop_num][i].handler?
				      iop_listeners[iop_num][i].devname : "");

	}
	len += sprintf(buf+len, "\n");
	return len;
}

static int iop_get_proc_info(char *buf, char **start, off_t pos, int count)
{
	int len, cnt;

	cnt = 0;
	len =  sprintf(buf, "IOPs detected:\n\n");

	if (iop_scc_present) {
		len += sprintf(buf+len, "SCC IOP (%p): status %02X\n",
				iop_base[IOP_NUM_SCC],
				(uint) iop_base[IOP_NUM_SCC]->status_ctrl);
	}
	if (iop_ism_present) {
		len += sprintf(buf+len, "ISM IOP (%p): status %02X\n\n",
				iop_base[IOP_NUM_ISM],
				(uint) iop_base[IOP_NUM_ISM]->status_ctrl);
	}

	if (iop_scc_present) {
		len += iop_dump_one_iop(buf+len, IOP_NUM_SCC, "SCC");

	}

	if (iop_ism_present) {
		len += iop_dump_one_iop(buf+len, IOP_NUM_ISM, "ISM");

	}

	if (len >= pos) {
		if (!*start) {
			*start = buf + pos;
			cnt = len - pos;
		} else {
			cnt += len;
		}
	}
	return (count > cnt) ? cnt : count;
}

#endif /* CONFIG_PROC_FS */
