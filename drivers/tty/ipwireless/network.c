/*
 * IPWireless 3G PCMCIA Network Driver
 *
 * Original code
 *   by Stephen Blackheath <stephen@blacksapphire.com>,
 *      Ben Martel <benm@symmetric.co.nz>
 *
 * Copyrighted as follows:
 *   Copyright (C) 2004 by Symmetric Systems Ltd (NZ)
 *
 * Various driver changes and rewrites, port to new kernels
 *   Copyright (C) 2006-2007 Jiri Kosina
 *
 * Misc code cleanups and updates
 *   Copyright (C) 2007 David Sterba
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/ppp_channel.h>
#include <linux/ppp_defs.h>
#include <linux/slab.h>
#include <linux/ppp-ioctl.h>
#include <linux/skbuff.h>

#include "network.h"
#include "hardware.h"
#include "main.h"
#include "tty.h"

#define MAX_ASSOCIATED_TTYS 2

#define SC_RCV_BITS     (SC_RCV_B7_1|SC_RCV_B7_0|SC_RCV_ODDP|SC_RCV_EVNP)

struct ipw_network {
	/* Hardware context, used for calls to hardware layer. */
	struct ipw_hardware *hardware;
	/* Context for kernel 'generic_ppp' functionality */
	struct ppp_channel *ppp_channel;
	/* tty context connected with IPW console */
	struct ipw_tty *associated_ttys[NO_OF_IPW_CHANNELS][MAX_ASSOCIATED_TTYS];
	/* True if ppp needs waking up once we're ready to xmit */
	int ppp_blocked;
	/* Number of packets queued up in hardware module. */
	int outgoing_packets_queued;
	/* Spinlock to avoid interrupts during shutdown */
	spinlock_t lock;
	struct mutex close_lock;

	/* PPP ioctl data, not actually used anywere */
	unsigned int flags;
	unsigned int rbits;
	u32 xaccm[8];
	u32 raccm;
	int mru;

	int shutting_down;
	unsigned int ras_control_lines;

	struct work_struct work_go_online;
	struct work_struct work_go_offline;
};

static void notify_packet_sent(void *callback_data, unsigned int packet_length)
{
	struct ipw_network *network = callback_data;
	unsigned long flags;

	spin_lock_irqsave(&network->lock, flags);
	network->outgoing_packets_queued--;
	if (network->ppp_channel != NULL) {
		if (network->ppp_blocked) {
			network->ppp_blocked = 0;
			spin_unlock_irqrestore(&network->lock, flags);
			ppp_output_wakeup(network->ppp_channel);
			if (ipwireless_debug)
				printk(KERN_DEBUG IPWIRELESS_PCCARD_NAME
				       ": ppp unblocked\n");
		} else
			spin_unlock_irqrestore(&network->lock, flags);
	} else
		spin_unlock_irqrestore(&network->lock, flags);
}

/*
 * Called by the ppp system when it has a packet to send to the hardware.
 */
static int ipwireless_ppp_start_xmit(struct ppp_channel *ppp_channel,
				     struct sk_buff *skb)
{
	struct ipw_network *network = ppp_channel->private;
	unsigned long flags;

	spin_lock_irqsave(&network->lock, flags);
	if (network->outgoing_packets_queued < ipwireless_out_queue) {
		unsigned char *buf;
		static unsigned char header[] = {
			PPP_ALLSTATIONS, /* 0xff */
			PPP_UI,		 /* 0x03 */
		};
		int ret;

		network->outgoing_packets_queued++;
		spin_unlock_irqrestore(&network->lock, flags);

		/*
		 * If we have the requested amount of headroom in the skb we
		 * were handed, then we can add the header efficiently.
		 */
		if (skb_headroom(skb) >= 2) {
			memcpy(skb_push(skb, 2), header, 2);
			ret = ipwireless_send_packet(network->hardware,
					       IPW_CHANNEL_RAS, skb->data,
					       skb->len,
					       notify_packet_sent,
					       network);
			if (ret == -1) {
				skb_pull(skb, 2);
				return 0;
			}
		} else {
			/* Otherwise (rarely) we do it inefficiently. */
			buf = kmalloc(skb->len + 2, GFP_ATOMIC);
			if (!buf)
				return 0;
			memcpy(buf + 2, skb->data, skb->len);
			memcpy(buf, header, 2);
			ret = ipwireless_send_packet(network->hardware,
					       IPW_CHANNEL_RAS, buf,
					       skb->len + 2,
					       notify_packet_sent,
					       network);
			kfree(buf);
			if (ret == -1)
				return 0;
		}
		kfree_skb(skb);
		return 1;
	} else {
		/*
		 * Otherwise reject the packet, and flag that the ppp system
		 * needs to be unblocked once we are ready to send.
		 */
		network->ppp_blocked = 1;
		spin_unlock_irqrestore(&network->lock, flags);
		if (ipwireless_debug)
			printk(KERN_DEBUG IPWIRELESS_PCCARD_NAME ": ppp blocked\n");
		return 0;
	}
}

/* Handle an ioctl call that has come in via ppp. (copy of ppp_async_ioctl() */
static int ipwireless_ppp_ioctl(struct ppp_channel *ppp_channel,
				unsigned int cmd, unsigned long arg)
{
	struct ipw_network *network = ppp_channel->private;
	int err, val;
	u32 accm[8];
	int __user *user_arg = (int __user *) arg;

	err = -EFAULT;
	switch (cmd) {
	case PPPIOCGFLAGS:
		val = network->flags | network->rbits;
		if (put_user(val, user_arg))
			break;
		err = 0;
		break;

	case PPPIOCSFLAGS:
		if (get_user(val, user_arg))
			break;
		network->flags = val & ~SC_RCV_BITS;
		network->rbits = val & SC_RCV_BITS;
		err = 0;
		break;

	case PPPIOCGASYNCMAP:
		if (put_user(network->xaccm[0], user_arg))
			break;
		err = 0;
		break;

	case PPPIOCSASYNCMAP:
		if (get_user(network->xaccm[0], user_arg))
			break;
		err = 0;
		break;

	case PPPIOCGRASYNCMAP:
		if (put_user(network->raccm, user_arg))
			break;
		err = 0;
		break;

	case PPPIOCSRASYNCMAP:
		if (get_user(network->raccm, user_arg))
			break;
		err = 0;
		break;

	case PPPIOCGXASYNCMAP:
		if (copy_to_user((void __user *) arg, network->xaccm,
					sizeof(network->xaccm)))
			break;
		err = 0;
		break;

	case PPPIOCSXASYNCMAP:
		if (copy_from_user(accm, (void __user *) arg, sizeof(accm)))
			break;
		accm[2] &= ~0x40000000U;	/* can't escape 0x5e */
		accm[3] |= 0x60000000U;	/* must escape 0x7d, 0x7e */
		memcpy(network->xaccm, accm, sizeof(network->xaccm));
		err = 0;
		break;

	case PPPIOCGMRU:
		if (put_user(network->mru, user_arg))
			break;
		err = 0;
		break;

	case PPPIOCSMRU:
		if (get_user(val, user_arg))
			break;
		if (val < PPP_MRU)
			val = PPP_MRU;
		network->mru = val;
		err = 0;
		break;

	default:
		err = -ENOTTY;
	}

	return err;
}

static const struct ppp_channel_ops ipwireless_ppp_channel_ops = {
	.start_xmit = ipwireless_ppp_start_xmit,
	.ioctl      = ipwireless_ppp_ioctl
};

static void do_go_online(struct work_struct *work_go_online)
{
	struct ipw_network *network =
		container_of(work_go_online, struct ipw_network,
				work_go_online);
	unsigned long flags;

	spin_lock_irqsave(&network->lock, flags);
	if (!network->ppp_channel) {
		struct ppp_channel *channel;

		spin_unlock_irqrestore(&network->lock, flags);
		channel = kzalloc(sizeof(struct ppp_channel), GFP_KERNEL);
		if (!channel) {
			printk(KERN_ERR IPWIRELESS_PCCARD_NAME
					": unable to allocate PPP channel\n");
			return;
		}
		channel->private = network;
		channel->mtu = 16384;	/* Wild guess */
		channel->hdrlen = 2;
		channel->ops = &ipwireless_ppp_channel_ops;

		network->flags = 0;
		network->rbits = 0;
		network->mru = PPP_MRU;
		memset(network->xaccm, 0, sizeof(network->xaccm));
		network->xaccm[0] = ~0U;
		network->xaccm[3] = 0x60000000U;
		network->raccm = ~0U;
		ppp_register_channel(channel);
		spin_lock_irqsave(&network->lock, flags);
		network->ppp_channel = channel;
	}
	spin_unlock_irqrestore(&network->lock, flags);
}

static void do_go_offline(struct work_struct *work_go_offline)
{
	struct ipw_network *network =
		container_of(work_go_offline, struct ipw_network,
				work_go_offline);
	unsigned long flags;

	mutex_lock(&network->close_lock);
	spin_lock_irqsave(&network->lock, flags);
	if (network->ppp_channel != NULL) {
		struct ppp_channel *channel = network->ppp_channel;

		network->ppp_channel = NULL;
		spin_unlock_irqrestore(&network->lock, flags);
		mutex_unlock(&network->close_lock);
		ppp_unregister_channel(channel);
	} else {
		spin_unlock_irqrestore(&network->lock, flags);
		mutex_unlock(&network->close_lock);
	}
}

void ipwireless_network_notify_control_line_change(struct ipw_network *network,
						   unsigned int channel_idx,
						   unsigned int control_lines,
						   unsigned int changed_mask)
{
	int i;

	if (channel_idx == IPW_CHANNEL_RAS)
		network->ras_control_lines = control_lines;

	for (i = 0; i < MAX_ASSOCIATED_TTYS; i++) {
		struct ipw_tty *tty =
			network->associated_ttys[channel_idx][i];

		/*
		 * If it's associated with a tty (other than the RAS channel
		 * when we're online), then send the data to that tty.  The RAS
		 * channel's data is handled above - it always goes through
		 * ppp_generic.
		 */
		if (tty)
			ipwireless_tty_notify_control_line_change(tty,
								  channel_idx,
								  control_lines,
								  changed_mask);
	}
}

/*
 * Some versions of firmware stuff packets with 0xff 0x03 (PPP: ALLSTATIONS, UI)
 * bytes, which are required on sent packet, but not always present on received
 * packets
 */
static struct sk_buff *ipw_packet_received_skb(unsigned char *data,
					       unsigned int length)
{
	struct sk_buff *skb;

	if (length > 2 && data[0] == PPP_ALLSTATIONS && data[1] == PPP_UI) {
		length -= 2;
		data += 2;
	}

	skb = dev_alloc_skb(length + 4);
	skb_reserve(skb, 2);
	memcpy(skb_put(skb, length), data, length);

	return skb;
}

void ipwireless_network_packet_received(struct ipw_network *network,
					unsigned int channel_idx,
					unsigned char *data,
					unsigned int length)
{
	int i;
	unsigned long flags;

	for (i = 0; i < MAX_ASSOCIATED_TTYS; i++) {
		struct ipw_tty *tty = network->associated_ttys[channel_idx][i];

		if (!tty)
			continue;

		/*
		 * If it's associated with a tty (other than the RAS channel
		 * when we're online), then send the data to that tty.  The RAS
		 * channel's data is handled above - it always goes through
		 * ppp_generic.
		 */
		if (channel_idx == IPW_CHANNEL_RAS
				&& (network->ras_control_lines &
					IPW_CONTROL_LINE_DCD) != 0
				&& ipwireless_tty_is_modem(tty)) {
			/*
			 * If data came in on the RAS channel and this tty is
			 * the modem tty, and we are online, then we send it to
			 * the PPP layer.
			 */
			mutex_lock(&network->close_lock);
			spin_lock_irqsave(&network->lock, flags);
			if (network->ppp_channel != NULL) {
				struct sk_buff *skb;

				spin_unlock_irqrestore(&network->lock,
						flags);

				/* Send the data to the ppp_generic module. */
				skb = ipw_packet_received_skb(data, length);
				ppp_input(network->ppp_channel, skb);
			} else
				spin_unlock_irqrestore(&network->lock,
						flags);
			mutex_unlock(&network->close_lock);
		}
		/* Otherwise we send it out the tty. */
		else
			ipwireless_tty_received(tty, data, length);
	}
}

struct ipw_network *ipwireless_network_create(struct ipw_hardware *hw)
{
	struct ipw_network *network =
		kzalloc(sizeof(struct ipw_network), GFP_ATOMIC);

	if (!network)
		return NULL;

	spin_lock_init(&network->lock);
	mutex_init(&network->close_lock);

	network->hardware = hw;

	INIT_WORK(&network->work_go_online, do_go_online);
	INIT_WORK(&network->work_go_offline, do_go_offline);

	ipwireless_associate_network(hw, network);

	return network;
}

void ipwireless_network_free(struct ipw_network *network)
{
	network->shutting_down = 1;

	ipwireless_ppp_close(network);
	flush_work_sync(&network->work_go_online);
	flush_work_sync(&network->work_go_offline);

	ipwireless_stop_interrupts(network->hardware);
	ipwireless_associate_network(network->hardware, NULL);

	kfree(network);
}

void ipwireless_associate_network_tty(struct ipw_network *network,
				      unsigned int channel_idx,
				      struct ipw_tty *tty)
{
	int i;

	for (i = 0; i < MAX_ASSOCIATED_TTYS; i++)
		if (network->associated_ttys[channel_idx][i] == NULL) {
			network->associated_ttys[channel_idx][i] = tty;
			break;
		}
}

void ipwireless_disassociate_network_ttys(struct ipw_network *network,
					  unsigned int channel_idx)
{
	int i;

	for (i = 0; i < MAX_ASSOCIATED_TTYS; i++)
		network->associated_ttys[channel_idx][i] = NULL;
}

void ipwireless_ppp_open(struct ipw_network *network)
{
	if (ipwireless_debug)
		printk(KERN_DEBUG IPWIRELESS_PCCARD_NAME ": online\n");
	schedule_work(&network->work_go_online);
}

void ipwireless_ppp_close(struct ipw_network *network)
{
	/* Disconnect from the wireless network. */
	if (ipwireless_debug)
		printk(KERN_DEBUG IPWIRELESS_PCCARD_NAME ": offline\n");
	schedule_work(&network->work_go_offline);
}

int ipwireless_ppp_channel_index(struct ipw_network *network)
{
	int ret = -1;
	unsigned long flags;

	spin_lock_irqsave(&network->lock, flags);
	if (network->ppp_channel != NULL)
		ret = ppp_channel_index(network->ppp_channel);
	spin_unlock_irqrestore(&network->lock, flags);

	return ret;
}

int ipwireless_ppp_unit_number(struct ipw_network *network)
{
	int ret = -1;
	unsigned long flags;

	spin_lock_irqsave(&network->lock, flags);
	if (network->ppp_channel != NULL)
		ret = ppp_unit_number(network->ppp_channel);
	spin_unlock_irqrestore(&network->lock, flags);

	return ret;
}

int ipwireless_ppp_mru(const struct ipw_network *network)
{
	return network->mru;
}
