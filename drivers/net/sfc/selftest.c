/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel_stat.h>
#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/rtnetlink.h>
#include <asm/io.h>
#include "net_driver.h"
#include "ethtool.h"
#include "efx.h"
#include "falcon.h"
#include "selftest.h"
#include "boards.h"
#include "workarounds.h"
#include "spi.h"
#include "falcon_io.h"
#include "mdio_10g.h"

/*
 * Loopback test packet structure
 *
 * The self-test should stress every RSS vector, and unfortunately
 * Falcon only performs RSS on TCP/UDP packets.
 */
struct efx_loopback_payload {
	struct ethhdr header;
	struct iphdr ip;
	struct udphdr udp;
	__be16 iteration;
	const char msg[64];
} __attribute__ ((packed));

/* Loopback test source MAC address */
static const unsigned char payload_source[ETH_ALEN] = {
	0x00, 0x0f, 0x53, 0x1b, 0x1b, 0x1b,
};

static const char *payload_msg =
	"Hello world! This is an Efx loopback test in progress!";

/**
 * efx_loopback_state - persistent state during a loopback selftest
 * @flush:		Drop all packets in efx_loopback_rx_packet
 * @packet_count:	Number of packets being used in this test
 * @skbs:		An array of skbs transmitted
 * @rx_good:		RX good packet count
 * @rx_bad:		RX bad packet count
 * @payload:		Payload used in tests
 */
struct efx_loopback_state {
	bool flush;
	int packet_count;
	struct sk_buff **skbs;

	/* Checksums are being offloaded */
	bool offload_csum;

	atomic_t rx_good;
	atomic_t rx_bad;
	struct efx_loopback_payload payload;
};

/**************************************************************************
 *
 * MII, NVRAM and register tests
 *
 **************************************************************************/

static int efx_test_mdio(struct efx_nic *efx, struct efx_self_tests *tests)
{
	int rc = 0;
	int devad = __ffs(efx->mdio.mmds);
	u16 physid1, physid2;

	if (efx->phy_type == PHY_TYPE_NONE)
		return 0;

	mutex_lock(&efx->mac_lock);
	tests->mdio = -1;

	physid1 = efx_mdio_read(efx, devad, MDIO_DEVID1);
	physid2 = efx_mdio_read(efx, devad, MDIO_DEVID2);

	if ((physid1 == 0x0000) || (physid1 == 0xffff) ||
	    (physid2 == 0x0000) || (physid2 == 0xffff)) {
		EFX_ERR(efx, "no MDIO PHY present with ID %d\n",
			efx->mdio.prtad);
		rc = -EINVAL;
		goto out;
	}

	if (EFX_IS10G(efx)) {
		rc = efx_mdio_check_mmds(efx, efx->phy_op->mmds, 0);
		if (rc)
			goto out;
	}

out:
	mutex_unlock(&efx->mac_lock);
	tests->mdio = rc ? -1 : 1;
	return rc;
}

static int efx_test_nvram(struct efx_nic *efx, struct efx_self_tests *tests)
{
	int rc;

	rc = falcon_read_nvram(efx, NULL);
	tests->nvram = rc ? -1 : 1;
	return rc;
}

static int efx_test_chip(struct efx_nic *efx, struct efx_self_tests *tests)
{
	int rc;

	/* Not supported on A-series silicon */
	if (falcon_rev(efx) < FALCON_REV_B0)
		return 0;

	rc = falcon_test_registers(efx);
	tests->registers = rc ? -1 : 1;
	return rc;
}

/**************************************************************************
 *
 * Interrupt and event queue testing
 *
 **************************************************************************/

/* Test generation and receipt of interrupts */
static int efx_test_interrupts(struct efx_nic *efx,
			       struct efx_self_tests *tests)
{
	struct efx_channel *channel;

	EFX_LOG(efx, "testing interrupts\n");
	tests->interrupt = -1;

	/* Reset interrupt flag */
	efx->last_irq_cpu = -1;
	smp_wmb();

	/* ACK each interrupting event queue. Receiving an interrupt due to
	 * traffic before a test event is raised is considered a pass */
	efx_for_each_channel(channel, efx) {
		if (channel->work_pending)
			efx_process_channel_now(channel);
		if (efx->last_irq_cpu >= 0)
			goto success;
	}

	falcon_generate_interrupt(efx);

	/* Wait for arrival of test interrupt. */
	EFX_LOG(efx, "waiting for test interrupt\n");
	schedule_timeout_uninterruptible(HZ / 10);
	if (efx->last_irq_cpu >= 0)
		goto success;

	EFX_ERR(efx, "timed out waiting for interrupt\n");
	return -ETIMEDOUT;

 success:
	EFX_LOG(efx, "test interrupt (mode %d) seen on CPU%d\n",
		efx->interrupt_mode, efx->last_irq_cpu);
	tests->interrupt = 1;
	return 0;
}

/* Test generation and receipt of interrupting events */
static int efx_test_eventq_irq(struct efx_channel *channel,
			       struct efx_self_tests *tests)
{
	unsigned int magic, count;

	/* Channel specific code, limited to 20 bits */
	magic = (0x00010150 + channel->channel);
	EFX_LOG(channel->efx, "channel %d testing event queue with code %x\n",
		channel->channel, magic);

	tests->eventq_dma[channel->channel] = -1;
	tests->eventq_int[channel->channel] = -1;
	tests->eventq_poll[channel->channel] = -1;

	/* Reset flag and zero magic word */
	channel->efx->last_irq_cpu = -1;
	channel->eventq_magic = 0;
	smp_wmb();

	falcon_generate_test_event(channel, magic);

	/* Wait for arrival of interrupt */
	count = 0;
	do {
		schedule_timeout_uninterruptible(HZ / 100);

		if (channel->work_pending)
			efx_process_channel_now(channel);

		if (channel->eventq_magic == magic)
			goto eventq_ok;
	} while (++count < 2);

	EFX_ERR(channel->efx, "channel %d timed out waiting for event queue\n",
		channel->channel);

	/* See if interrupt arrived */
	if (channel->efx->last_irq_cpu >= 0) {
		EFX_ERR(channel->efx, "channel %d saw interrupt on CPU%d "
			"during event queue test\n", channel->channel,
			raw_smp_processor_id());
		tests->eventq_int[channel->channel] = 1;
	}

	/* Check to see if event was received even if interrupt wasn't */
	efx_process_channel_now(channel);
	if (channel->eventq_magic == magic) {
		EFX_ERR(channel->efx, "channel %d event was generated, but "
			"failed to trigger an interrupt\n", channel->channel);
		tests->eventq_dma[channel->channel] = 1;
	}

	return -ETIMEDOUT;
 eventq_ok:
	EFX_LOG(channel->efx, "channel %d event queue passed\n",
		channel->channel);
	tests->eventq_dma[channel->channel] = 1;
	tests->eventq_int[channel->channel] = 1;
	tests->eventq_poll[channel->channel] = 1;
	return 0;
}

static int efx_test_phy(struct efx_nic *efx, struct efx_self_tests *tests,
			unsigned flags)
{
	int rc;

	if (!efx->phy_op->run_tests)
		return 0;

	EFX_BUG_ON_PARANOID(efx->phy_op->num_tests == 0 ||
			    efx->phy_op->num_tests > EFX_MAX_PHY_TESTS);

	mutex_lock(&efx->mac_lock);
	rc = efx->phy_op->run_tests(efx, tests->phy, flags);
	mutex_unlock(&efx->mac_lock);
	return rc;
}

/**************************************************************************
 *
 * Loopback testing
 * NB Only one loopback test can be executing concurrently.
 *
 **************************************************************************/

/* Loopback test RX callback
 * This is called for each received packet during loopback testing.
 */
void efx_loopback_rx_packet(struct efx_nic *efx,
			    const char *buf_ptr, int pkt_len)
{
	struct efx_loopback_state *state = efx->loopback_selftest;
	struct efx_loopback_payload *received;
	struct efx_loopback_payload *payload;

	BUG_ON(!buf_ptr);

	/* If we are just flushing, then drop the packet */
	if ((state == NULL) || state->flush)
		return;

	payload = &state->payload;

	received = (struct efx_loopback_payload *) buf_ptr;
	received->ip.saddr = payload->ip.saddr;
	if (state->offload_csum)
		received->ip.check = payload->ip.check;

	/* Check that header exists */
	if (pkt_len < sizeof(received->header)) {
		EFX_ERR(efx, "saw runt RX packet (length %d) in %s loopback "
			"test\n", pkt_len, LOOPBACK_MODE(efx));
		goto err;
	}

	/* Check that the ethernet header exists */
	if (memcmp(&received->header, &payload->header, ETH_HLEN) != 0) {
		EFX_ERR(efx, "saw non-loopback RX packet in %s loopback test\n",
			LOOPBACK_MODE(efx));
		goto err;
	}

	/* Check packet length */
	if (pkt_len != sizeof(*payload)) {
		EFX_ERR(efx, "saw incorrect RX packet length %d (wanted %d) in "
			"%s loopback test\n", pkt_len, (int)sizeof(*payload),
			LOOPBACK_MODE(efx));
		goto err;
	}

	/* Check that IP header matches */
	if (memcmp(&received->ip, &payload->ip, sizeof(payload->ip)) != 0) {
		EFX_ERR(efx, "saw corrupted IP header in %s loopback test\n",
			LOOPBACK_MODE(efx));
		goto err;
	}

	/* Check that msg and padding matches */
	if (memcmp(&received->msg, &payload->msg, sizeof(received->msg)) != 0) {
		EFX_ERR(efx, "saw corrupted RX packet in %s loopback test\n",
			LOOPBACK_MODE(efx));
		goto err;
	}

	/* Check that iteration matches */
	if (received->iteration != payload->iteration) {
		EFX_ERR(efx, "saw RX packet from iteration %d (wanted %d) in "
			"%s loopback test\n", ntohs(received->iteration),
			ntohs(payload->iteration), LOOPBACK_MODE(efx));
		goto err;
	}

	/* Increase correct RX count */
	EFX_TRACE(efx, "got loopback RX in %s loopback test\n",
		  LOOPBACK_MODE(efx));

	atomic_inc(&state->rx_good);
	return;

 err:
#ifdef EFX_ENABLE_DEBUG
	if (atomic_read(&state->rx_bad) == 0) {
		EFX_ERR(efx, "received packet:\n");
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 0x10, 1,
			       buf_ptr, pkt_len, 0);
		EFX_ERR(efx, "expected packet:\n");
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 0x10, 1,
			       &state->payload, sizeof(state->payload), 0);
	}
#endif
	atomic_inc(&state->rx_bad);
}

/* Initialise an efx_selftest_state for a new iteration */
static void efx_iterate_state(struct efx_nic *efx)
{
	struct efx_loopback_state *state = efx->loopback_selftest;
	struct net_device *net_dev = efx->net_dev;
	struct efx_loopback_payload *payload = &state->payload;

	/* Initialise the layerII header */
	memcpy(&payload->header.h_dest, net_dev->dev_addr, ETH_ALEN);
	memcpy(&payload->header.h_source, &payload_source, ETH_ALEN);
	payload->header.h_proto = htons(ETH_P_IP);

	/* saddr set later and used as incrementing count */
	payload->ip.daddr = htonl(INADDR_LOOPBACK);
	payload->ip.ihl = 5;
	payload->ip.check = htons(0xdead);
	payload->ip.tot_len = htons(sizeof(*payload) - sizeof(struct ethhdr));
	payload->ip.version = IPVERSION;
	payload->ip.protocol = IPPROTO_UDP;

	/* Initialise udp header */
	payload->udp.source = 0;
	payload->udp.len = htons(sizeof(*payload) - sizeof(struct ethhdr) -
				 sizeof(struct iphdr));
	payload->udp.check = 0;	/* checksum ignored */

	/* Fill out payload */
	payload->iteration = htons(ntohs(payload->iteration) + 1);
	memcpy(&payload->msg, payload_msg, sizeof(payload_msg));

	/* Fill out remaining state members */
	atomic_set(&state->rx_good, 0);
	atomic_set(&state->rx_bad, 0);
	smp_wmb();
}

static int efx_begin_loopback(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	struct efx_loopback_state *state = efx->loopback_selftest;
	struct efx_loopback_payload *payload;
	struct sk_buff *skb;
	int i;
	netdev_tx_t rc;

	/* Transmit N copies of buffer */
	for (i = 0; i < state->packet_count; i++) {
		/* Allocate an skb, holding an extra reference for
		 * transmit completion counting */
		skb = alloc_skb(sizeof(state->payload), GFP_KERNEL);
		if (!skb)
			return -ENOMEM;
		state->skbs[i] = skb;
		skb_get(skb);

		/* Copy the payload in, incrementing the source address to
		 * exercise the rss vectors */
		payload = ((struct efx_loopback_payload *)
			   skb_put(skb, sizeof(state->payload)));
		memcpy(payload, &state->payload, sizeof(state->payload));
		payload->ip.saddr = htonl(INADDR_LOOPBACK | (i << 2));

		/* Ensure everything we've written is visible to the
		 * interrupt handler. */
		smp_wmb();

		if (efx_dev_registered(efx))
			netif_tx_lock_bh(efx->net_dev);
		rc = efx_xmit(efx, tx_queue, skb);
		if (efx_dev_registered(efx))
			netif_tx_unlock_bh(efx->net_dev);

		if (rc != NETDEV_TX_OK) {
			EFX_ERR(efx, "TX queue %d could not transmit packet %d "
				"of %d in %s loopback test\n", tx_queue->queue,
				i + 1, state->packet_count, LOOPBACK_MODE(efx));

			/* Defer cleaning up the other skbs for the caller */
			kfree_skb(skb);
			return -EPIPE;
		}
		efx->net_dev->trans_start = jiffies;
	}

	return 0;
}

static int efx_poll_loopback(struct efx_nic *efx)
{
	struct efx_loopback_state *state = efx->loopback_selftest;
	struct efx_channel *channel;

	/* NAPI polling is not enabled, so process channels
	 * synchronously */
	efx_for_each_channel(channel, efx) {
		if (channel->work_pending)
			efx_process_channel_now(channel);
	}
	return atomic_read(&state->rx_good) == state->packet_count;
}

static int efx_end_loopback(struct efx_tx_queue *tx_queue,
			    struct efx_loopback_self_tests *lb_tests)
{
	struct efx_nic *efx = tx_queue->efx;
	struct efx_loopback_state *state = efx->loopback_selftest;
	struct sk_buff *skb;
	int tx_done = 0, rx_good, rx_bad;
	int i, rc = 0;

	if (efx_dev_registered(efx))
		netif_tx_lock_bh(efx->net_dev);

	/* Count the number of tx completions, and decrement the refcnt. Any
	 * skbs not already completed will be free'd when the queue is flushed */
	for (i=0; i < state->packet_count; i++) {
		skb = state->skbs[i];
		if (skb && !skb_shared(skb))
			++tx_done;
		dev_kfree_skb_any(skb);
	}

	if (efx_dev_registered(efx))
		netif_tx_unlock_bh(efx->net_dev);

	/* Check TX completion and received packet counts */
	rx_good = atomic_read(&state->rx_good);
	rx_bad = atomic_read(&state->rx_bad);
	if (tx_done != state->packet_count) {
		/* Don't free the skbs; they will be picked up on TX
		 * overflow or channel teardown.
		 */
		EFX_ERR(efx, "TX queue %d saw only %d out of an expected %d "
			"TX completion events in %s loopback test\n",
			tx_queue->queue, tx_done, state->packet_count,
			LOOPBACK_MODE(efx));
		rc = -ETIMEDOUT;
		/* Allow to fall through so we see the RX errors as well */
	}

	/* We may always be up to a flush away from our desired packet total */
	if (rx_good != state->packet_count) {
		EFX_LOG(efx, "TX queue %d saw only %d out of an expected %d "
			"received packets in %s loopback test\n",
			tx_queue->queue, rx_good, state->packet_count,
			LOOPBACK_MODE(efx));
		rc = -ETIMEDOUT;
		/* Fall through */
	}

	/* Update loopback test structure */
	lb_tests->tx_sent[tx_queue->queue] += state->packet_count;
	lb_tests->tx_done[tx_queue->queue] += tx_done;
	lb_tests->rx_good += rx_good;
	lb_tests->rx_bad += rx_bad;

	return rc;
}

static int
efx_test_loopback(struct efx_tx_queue *tx_queue,
		  struct efx_loopback_self_tests *lb_tests)
{
	struct efx_nic *efx = tx_queue->efx;
	struct efx_loopback_state *state = efx->loopback_selftest;
	int i, begin_rc, end_rc;

	for (i = 0; i < 3; i++) {
		/* Determine how many packets to send */
		state->packet_count = (efx->type->txd_ring_mask + 1) / 3;
		state->packet_count = min(1 << (i << 2), state->packet_count);
		state->skbs = kzalloc(sizeof(state->skbs[0]) *
				      state->packet_count, GFP_KERNEL);
		if (!state->skbs)
			return -ENOMEM;
		state->flush = false;

		EFX_LOG(efx, "TX queue %d testing %s loopback with %d "
			"packets\n", tx_queue->queue, LOOPBACK_MODE(efx),
			state->packet_count);

		efx_iterate_state(efx);
		begin_rc = efx_begin_loopback(tx_queue);

		/* This will normally complete very quickly, but be
		 * prepared to wait up to 100 ms. */
		msleep(1);
		if (!efx_poll_loopback(efx)) {
			msleep(100);
			efx_poll_loopback(efx);
		}

		end_rc = efx_end_loopback(tx_queue, lb_tests);
		kfree(state->skbs);

		if (begin_rc || end_rc) {
			/* Wait a while to ensure there are no packets
			 * floating around after a failure. */
			schedule_timeout_uninterruptible(HZ / 10);
			return begin_rc ? begin_rc : end_rc;
		}
	}

	EFX_LOG(efx, "TX queue %d passed %s loopback test with a burst length "
		"of %d packets\n", tx_queue->queue, LOOPBACK_MODE(efx),
		state->packet_count);

	return 0;
}

static int efx_test_loopbacks(struct efx_nic *efx, struct efx_self_tests *tests,
			      unsigned int loopback_modes)
{
	enum efx_loopback_mode mode;
	struct efx_loopback_state *state;
	struct efx_tx_queue *tx_queue;
	bool link_up;
	int count, rc = 0;

	/* Set the port loopback_selftest member. From this point on
	 * all received packets will be dropped. Mark the state as
	 * "flushing" so all inflight packets are dropped */
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	BUG_ON(efx->loopback_selftest);
	state->flush = true;
	efx->loopback_selftest = state;

	/* Test all supported loopback modes */
	for (mode = LOOPBACK_NONE; mode <= LOOPBACK_TEST_MAX; mode++) {
		if (!(loopback_modes & (1 << mode)))
			continue;

		/* Move the port into the specified loopback mode. */
		state->flush = true;
		efx->loopback_mode = mode;
		efx_reconfigure_port(efx);

		/* Wait for the PHY to signal the link is up. Interrupts
		 * are enabled for PHY's using LASI, otherwise we poll()
		 * quickly */
		count = 0;
		do {
			struct efx_channel *channel = &efx->channel[0];

			efx->phy_op->poll(efx);
			schedule_timeout_uninterruptible(HZ / 10);
			if (channel->work_pending)
				efx_process_channel_now(channel);
			/* Wait for PHY events to be processed */
			flush_workqueue(efx->workqueue);
			rmb();

			/* We need both the phy and xaui links to be ok.
			 * rather than relying on the falcon_xmac irq/poll
			 * regime, just poll xaui directly */
			link_up = efx->link_up;
			if (link_up && EFX_IS10G(efx) &&
			    !falcon_xaui_link_ok(efx))
				link_up = false;

		} while ((++count < 20) && !link_up);

		/* The link should now be up. If it isn't, there is no point
		 * in attempting a loopback test */
		if (!link_up) {
			EFX_ERR(efx, "loopback %s never came up\n",
				LOOPBACK_MODE(efx));
			rc = -EIO;
			goto out;
		}

		EFX_LOG(efx, "link came up in %s loopback in %d iterations\n",
			LOOPBACK_MODE(efx), count);

		/* Test every TX queue */
		efx_for_each_tx_queue(tx_queue, efx) {
			state->offload_csum = (tx_queue->queue ==
					       EFX_TX_QUEUE_OFFLOAD_CSUM);
			rc = efx_test_loopback(tx_queue,
					       &tests->loopback[mode]);
			if (rc)
				goto out;
		}
	}

 out:
	/* Remove the flush. The caller will remove the loopback setting */
	state->flush = true;
	efx->loopback_selftest = NULL;
	wmb();
	kfree(state);

	return rc;
}

/**************************************************************************
 *
 * Entry point
 *
 *************************************************************************/

int efx_selftest(struct efx_nic *efx, struct efx_self_tests *tests,
		 unsigned flags)
{
	enum efx_loopback_mode loopback_mode = efx->loopback_mode;
	int phy_mode = efx->phy_mode;
	enum reset_type reset_method = RESET_TYPE_INVISIBLE;
	struct ethtool_cmd ecmd;
	struct efx_channel *channel;
	int rc_test = 0, rc_reset = 0, rc;

	/* Online (i.e. non-disruptive) testing
	 * This checks interrupt generation, event delivery and PHY presence. */

	rc = efx_test_mdio(efx, tests);
	if (rc && !rc_test)
		rc_test = rc;

	rc = efx_test_nvram(efx, tests);
	if (rc && !rc_test)
		rc_test = rc;

	rc = efx_test_interrupts(efx, tests);
	if (rc && !rc_test)
		rc_test = rc;

	efx_for_each_channel(channel, efx) {
		rc = efx_test_eventq_irq(channel, tests);
		if (rc && !rc_test)
			rc_test = rc;
	}

	if (rc_test)
		return rc_test;

	if (!(flags & ETH_TEST_FL_OFFLINE))
		return efx_test_phy(efx, tests, flags);

	/* Offline (i.e. disruptive) testing
	 * This checks MAC and PHY loopback on the specified port. */

	/* force the carrier state off so the kernel doesn't transmit during
	 * the loopback test, and the watchdog timeout doesn't fire. Also put
	 * falcon into loopback for the register test.
	 */
	mutex_lock(&efx->mac_lock);
	efx->port_inhibited = true;
	if (efx->loopback_modes) {
		/* We need the 312 clock from the PHY to test the XMAC
		 * registers, so move into XGMII loopback if available */
		if (efx->loopback_modes & (1 << LOOPBACK_XGMII))
			efx->loopback_mode = LOOPBACK_XGMII;
		else
			efx->loopback_mode = __ffs(efx->loopback_modes);
	}

	__efx_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);

	/* free up all consumers of SRAM (including all the queues) */
	efx_reset_down(efx, reset_method, &ecmd);

	rc = efx_test_chip(efx, tests);
	if (rc && !rc_test)
		rc_test = rc;

	/* reset the chip to recover from the register test */
	rc_reset = falcon_reset_hw(efx, reset_method);

	/* Ensure that the phy is powered and out of loopback
	 * for the bist and loopback tests */
	efx->phy_mode &= ~PHY_MODE_LOW_POWER;
	efx->loopback_mode = LOOPBACK_NONE;

	rc = efx_reset_up(efx, reset_method, &ecmd, rc_reset == 0);
	if (rc && !rc_reset)
		rc_reset = rc;

	if (rc_reset) {
		EFX_ERR(efx, "Unable to recover from chip test\n");
		efx_schedule_reset(efx, RESET_TYPE_DISABLE);
		return rc_reset;
	}

	rc = efx_test_phy(efx, tests, flags);
	if (rc && !rc_test)
		rc_test = rc;

	rc = efx_test_loopbacks(efx, tests, efx->loopback_modes);
	if (rc && !rc_test)
		rc_test = rc;

	/* restore the PHY to the previous state */
	efx->loopback_mode = loopback_mode;
	efx->phy_mode = phy_mode;
	efx->port_inhibited = false;
	efx_ethtool_set_settings(efx->net_dev, &ecmd);

	return rc_test;
}

