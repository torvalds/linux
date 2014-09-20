/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/types.h>
#include <linux/module.h>
#include <net/ipv6.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <linux/if_macvlan.h>

#include "fm10k.h"

#define DRV_VERSION	"0.12.2-k"
const char fm10k_driver_version[] = DRV_VERSION;
char fm10k_driver_name[] = "fm10k";
static const char fm10k_driver_string[] =
	"Intel(R) Ethernet Switch Host Interface Driver";
static const char fm10k_copyright[] =
	"Copyright (c) 2013 Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) Ethernet Switch Host Interface Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * fm10k_init_module - Driver Registration Routine
 *
 * fm10k_init_module is the first routine called when the driver is
 * loaded.  All it does is register with the PCI subsystem.
 **/
static int __init fm10k_init_module(void)
{
	pr_info("%s - version %s\n", fm10k_driver_string, fm10k_driver_version);
	pr_info("%s\n", fm10k_copyright);

	return fm10k_register_pci_driver();
}
module_init(fm10k_init_module);

/**
 * fm10k_exit_module - Driver Exit Cleanup Routine
 *
 * fm10k_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit fm10k_exit_module(void)
{
	fm10k_unregister_pci_driver();
}
module_exit(fm10k_exit_module);

/**
 * fm10k_update_itr - update the dynamic ITR value based on packet size
 *
 *      Stores a new ITR value based on strictly on packet size.  The
 *      divisors and thresholds used by this function were determined based
 *      on theoretical maximum wire speed and testing data, in order to
 *      minimize response time while increasing bulk throughput.
 *
 * @ring_container: Container for rings to have ITR updated
 **/
static void fm10k_update_itr(struct fm10k_ring_container *ring_container)
{
	unsigned int avg_wire_size, packets;

	/* Only update ITR if we are using adaptive setting */
	if (!(ring_container->itr & FM10K_ITR_ADAPTIVE))
		goto clear_counts;

	packets = ring_container->total_packets;
	if (!packets)
		goto clear_counts;

	avg_wire_size = ring_container->total_bytes / packets;

	/* Add 24 bytes to size to account for CRC, preamble, and gap */
	avg_wire_size += 24;

	/* Don't starve jumbo frames */
	if (avg_wire_size > 3000)
		avg_wire_size = 3000;

	/* Give a little boost to mid-size frames */
	if ((avg_wire_size > 300) && (avg_wire_size < 1200))
		avg_wire_size /= 3;
	else
		avg_wire_size /= 2;

	/* write back value and retain adaptive flag */
	ring_container->itr = avg_wire_size | FM10K_ITR_ADAPTIVE;

clear_counts:
	ring_container->total_bytes = 0;
	ring_container->total_packets = 0;
}

static void fm10k_qv_enable(struct fm10k_q_vector *q_vector)
{
	/* Enable auto-mask and clear the current mask */
	u32 itr = FM10K_ITR_ENABLE;

	/* Update Tx ITR */
	fm10k_update_itr(&q_vector->tx);

	/* Update Rx ITR */
	fm10k_update_itr(&q_vector->rx);

	/* Store Tx itr in timer slot 0 */
	itr |= (q_vector->tx.itr & FM10K_ITR_MAX);

	/* Shift Rx itr to timer slot 1 */
	itr |= (q_vector->rx.itr & FM10K_ITR_MAX) << FM10K_ITR_INTERVAL1_SHIFT;

	/* Write the final value to the ITR register */
	writel(itr, q_vector->itr);
}

static int fm10k_poll(struct napi_struct *napi, int budget)
{
	struct fm10k_q_vector *q_vector =
			       container_of(napi, struct fm10k_q_vector, napi);

	/* all work done, exit the polling mode */
	napi_complete(napi);

	/* re-enable the q_vector */
	fm10k_qv_enable(q_vector);

	return 0;
}

/**
 * fm10k_set_num_queues: Allocate queues for device, feature dependent
 * @interface: board private structure to initialize
 *
 * This is the top level queue allocation routine.  The order here is very
 * important, starting with the "most" number of features turned on at once,
 * and ending with the smallest set of features.  This way large combinations
 * can be allocated if they're turned on, and smaller combinations are the
 * fallthrough conditions.
 *
 **/
static void fm10k_set_num_queues(struct fm10k_intfc *interface)
{
	/* Start with base case */
	interface->num_rx_queues = 1;
	interface->num_tx_queues = 1;
}

/**
 * fm10k_alloc_q_vector - Allocate memory for a single interrupt vector
 * @interface: board private structure to initialize
 * @v_count: q_vectors allocated on interface, used for ring interleaving
 * @v_idx: index of vector in interface struct
 * @txr_count: total number of Tx rings to allocate
 * @txr_idx: index of first Tx ring to allocate
 * @rxr_count: total number of Rx rings to allocate
 * @rxr_idx: index of first Rx ring to allocate
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int fm10k_alloc_q_vector(struct fm10k_intfc *interface,
				unsigned int v_count, unsigned int v_idx,
				unsigned int txr_count, unsigned int txr_idx,
				unsigned int rxr_count, unsigned int rxr_idx)
{
	struct fm10k_q_vector *q_vector;
	struct fm10k_ring *ring;
	int ring_count, size;

	ring_count = txr_count + rxr_count;
	size = sizeof(struct fm10k_q_vector) +
	       (sizeof(struct fm10k_ring) * ring_count);

	/* allocate q_vector and rings */
	q_vector = kzalloc(size, GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	/* initialize NAPI */
	netif_napi_add(interface->netdev, &q_vector->napi,
		       fm10k_poll, NAPI_POLL_WEIGHT);

	/* tie q_vector and interface together */
	interface->q_vector[v_idx] = q_vector;
	q_vector->interface = interface;
	q_vector->v_idx = v_idx;

	/* initialize pointer to rings */
	ring = q_vector->ring;

	/* save Tx ring container info */
	q_vector->tx.ring = ring;
	q_vector->tx.work_limit = FM10K_DEFAULT_TX_WORK;
	q_vector->tx.itr = interface->tx_itr;
	q_vector->tx.count = txr_count;

	while (txr_count) {
		/* assign generic ring traits */
		ring->dev = &interface->pdev->dev;
		ring->netdev = interface->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* apply Tx specific ring traits */
		ring->count = interface->tx_ring_count;
		ring->queue_index = txr_idx;

		/* assign ring to interface */
		interface->tx_ring[txr_idx] = ring;

		/* update count and index */
		txr_count--;
		txr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	/* save Rx ring container info */
	q_vector->rx.ring = ring;
	q_vector->rx.itr = interface->rx_itr;
	q_vector->rx.count = rxr_count;

	while (rxr_count) {
		/* assign generic ring traits */
		ring->dev = &interface->pdev->dev;
		ring->netdev = interface->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* apply Rx specific ring traits */
		ring->count = interface->rx_ring_count;
		ring->queue_index = rxr_idx;

		/* assign ring to interface */
		interface->rx_ring[rxr_idx] = ring;

		/* update count and index */
		rxr_count--;
		rxr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	return 0;
}

/**
 * fm10k_free_q_vector - Free memory allocated for specific interrupt vector
 * @interface: board private structure to initialize
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void fm10k_free_q_vector(struct fm10k_intfc *interface, int v_idx)
{
	struct fm10k_q_vector *q_vector = interface->q_vector[v_idx];
	struct fm10k_ring *ring;

	fm10k_for_each_ring(ring, q_vector->tx)
		interface->tx_ring[ring->queue_index] = NULL;

	fm10k_for_each_ring(ring, q_vector->rx)
		interface->rx_ring[ring->queue_index] = NULL;

	interface->q_vector[v_idx] = NULL;
	netif_napi_del(&q_vector->napi);
	kfree_rcu(q_vector, rcu);
}

/**
 * fm10k_alloc_q_vectors - Allocate memory for interrupt vectors
 * @interface: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int fm10k_alloc_q_vectors(struct fm10k_intfc *interface)
{
	unsigned int q_vectors = interface->num_q_vectors;
	unsigned int rxr_remaining = interface->num_rx_queues;
	unsigned int txr_remaining = interface->num_tx_queues;
	unsigned int rxr_idx = 0, txr_idx = 0, v_idx = 0;
	int err;

	if (q_vectors >= (rxr_remaining + txr_remaining)) {
		for (; rxr_remaining; v_idx++) {
			err = fm10k_alloc_q_vector(interface, q_vectors, v_idx,
						   0, 0, 1, rxr_idx);
			if (err)
				goto err_out;

			/* update counts and index */
			rxr_remaining--;
			rxr_idx++;
		}
	}

	for (; v_idx < q_vectors; v_idx++) {
		int rqpv = DIV_ROUND_UP(rxr_remaining, q_vectors - v_idx);
		int tqpv = DIV_ROUND_UP(txr_remaining, q_vectors - v_idx);

		err = fm10k_alloc_q_vector(interface, q_vectors, v_idx,
					   tqpv, txr_idx,
					   rqpv, rxr_idx);

		if (err)
			goto err_out;

		/* update counts and index */
		rxr_remaining -= rqpv;
		txr_remaining -= tqpv;
		rxr_idx++;
		txr_idx++;
	}

	return 0;

err_out:
	interface->num_tx_queues = 0;
	interface->num_rx_queues = 0;
	interface->num_q_vectors = 0;

	while (v_idx--)
		fm10k_free_q_vector(interface, v_idx);

	return -ENOMEM;
}

/**
 * fm10k_free_q_vectors - Free memory allocated for interrupt vectors
 * @interface: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void fm10k_free_q_vectors(struct fm10k_intfc *interface)
{
	int v_idx = interface->num_q_vectors;

	interface->num_tx_queues = 0;
	interface->num_rx_queues = 0;
	interface->num_q_vectors = 0;

	while (v_idx--)
		fm10k_free_q_vector(interface, v_idx);
}

/**
 * f10k_reset_msix_capability - reset MSI-X capability
 * @interface: board private structure to initialize
 *
 * Reset the MSI-X capability back to its starting state
 **/
static void fm10k_reset_msix_capability(struct fm10k_intfc *interface)
{
	pci_disable_msix(interface->pdev);
	kfree(interface->msix_entries);
	interface->msix_entries = NULL;
}

/**
 * f10k_init_msix_capability - configure MSI-X capability
 * @interface: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static int fm10k_init_msix_capability(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	int v_budget, vector;

	/* It's easy to be greedy for MSI-X vectors, but it really
	 * doesn't do us much good if we have a lot more vectors
	 * than CPU's.  So let's be conservative and only ask for
	 * (roughly) the same number of vectors as there are CPU's.
	 * the default is to use pairs of vectors
	 */
	v_budget = max(interface->num_rx_queues, interface->num_tx_queues);
	v_budget = min_t(u16, v_budget, num_online_cpus());

	/* account for vectors not related to queues */
	v_budget += NON_Q_VECTORS(hw);

	/* At the same time, hardware can only support a maximum of
	 * hw.mac->max_msix_vectors vectors.  With features
	 * such as RSS and VMDq, we can easily surpass the number of Rx and Tx
	 * descriptor queues supported by our device.  Thus, we cap it off in
	 * those rare cases where the cpu count also exceeds our vector limit.
	 */
	v_budget = min_t(int, v_budget, hw->mac.max_msix_vectors);

	/* A failure in MSI-X entry allocation is fatal. */
	interface->msix_entries = kcalloc(v_budget, sizeof(struct msix_entry),
					  GFP_KERNEL);
	if (!interface->msix_entries)
		return -ENOMEM;

	/* populate entry values */
	for (vector = 0; vector < v_budget; vector++)
		interface->msix_entries[vector].entry = vector;

	/* Attempt to enable MSI-X with requested value */
	v_budget = pci_enable_msix_range(interface->pdev,
					 interface->msix_entries,
					 MIN_MSIX_COUNT(hw),
					 v_budget);
	if (v_budget < 0) {
		kfree(interface->msix_entries);
		interface->msix_entries = NULL;
		return -ENOMEM;
	}

	/* record the number of queues available for q_vectors */
	interface->num_q_vectors = v_budget - NON_Q_VECTORS(hw);

	return 0;
}

static void fm10k_init_reta(struct fm10k_intfc *interface)
{
	u16 i, rss_i = interface->ring_feature[RING_F_RSS].indices;
	u32 reta, base;

	/* If the netdev is initialized we have to maintain table if possible */
	if (interface->netdev->reg_state) {
		for (i = FM10K_RETA_SIZE; i--;) {
			reta = interface->reta[i];
			if ((((reta << 24) >> 24) < rss_i) &&
			    (((reta << 16) >> 24) < rss_i) &&
			    (((reta <<  8) >> 24) < rss_i) &&
			    (((reta)       >> 24) < rss_i))
				continue;
			goto repopulate_reta;
		}

		/* do nothing if all of the elements are in bounds */
		return;
	}

repopulate_reta:
	/* Populate the redirection table 4 entries at a time.  To do this
	 * we are generating the results for n and n+2 and then interleaving
	 * those with the results with n+1 and n+3.
	 */
	for (i = FM10K_RETA_SIZE; i--;) {
		/* first pass generates n and n+2 */
		base = ((i * 0x00040004) + 0x00020000) * rss_i;
		reta = (base & 0x3F803F80) >> 7;

		/* second pass generates n+1 and n+3 */
		base += 0x00010001 * rss_i;
		reta |= (base & 0x3F803F80) << 1;

		interface->reta[i] = reta;
	}
}

/**
 * fm10k_init_queueing_scheme - Determine proper queueing scheme
 * @interface: board private structure to initialize
 *
 * We determine which queueing scheme to use based on...
 * - Hardware queue count (num_*_queues)
 *   - defined by miscellaneous hardware support/features (RSS, etc.)
 **/
int fm10k_init_queueing_scheme(struct fm10k_intfc *interface)
{
	int err;

	/* Number of supported queues */
	fm10k_set_num_queues(interface);

	/* Configure MSI-X capability */
	err = fm10k_init_msix_capability(interface);
	if (err) {
		dev_err(&interface->pdev->dev,
			"Unable to initialize MSI-X capability\n");
		return err;
	}

	/* Allocate memory for queues */
	err = fm10k_alloc_q_vectors(interface);
	if (err)
		return err;

	/* Initialize RSS redirection table */
	fm10k_init_reta(interface);

	return 0;
}

/**
 * fm10k_clear_queueing_scheme - Clear the current queueing scheme settings
 * @interface: board private structure to clear queueing scheme on
 *
 * We go through and clear queueing specific resources and reset the structure
 * to pre-load conditions
 **/
void fm10k_clear_queueing_scheme(struct fm10k_intfc *interface)
{
	fm10k_free_q_vectors(interface);
	fm10k_reset_msix_capability(interface);
}
