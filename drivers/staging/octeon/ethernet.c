/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
**********************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/mii.h>

#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "ethernet-mem.h"
#include "ethernet-rx.h"
#include "ethernet-tx.h"
#include "ethernet-util.h"
#include "ethernet-proc.h"
#include "ethernet-common.h"
#include "octeon-ethernet.h"

#include "cvmx-pip.h"
#include "cvmx-pko.h"
#include "cvmx-fau.h"
#include "cvmx-ipd.h"
#include "cvmx-helper.h"

#include "cvmx-smix-defs.h"

#if defined(CONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS) \
	&& CONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS
int num_packet_buffers = CONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS;
#else
int num_packet_buffers = 1024;
#endif
module_param(num_packet_buffers, int, 0444);
MODULE_PARM_DESC(num_packet_buffers, "\n"
	"\tNumber of packet buffers to allocate and store in the\n"
	"\tFPA. By default, 1024 packet buffers are used unless\n"
	"\tCONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS is defined.");

int pow_receive_group = 15;
module_param(pow_receive_group, int, 0444);
MODULE_PARM_DESC(pow_receive_group, "\n"
	"\tPOW group to receive packets from. All ethernet hardware\n"
	"\twill be configured to send incomming packets to this POW\n"
	"\tgroup. Also any other software can submit packets to this\n"
	"\tgroup for the kernel to process.");

int pow_send_group = -1;
module_param(pow_send_group, int, 0644);
MODULE_PARM_DESC(pow_send_group, "\n"
	"\tPOW group to send packets to other software on. This\n"
	"\tcontrols the creation of the virtual device pow0.\n"
	"\talways_use_pow also depends on this value.");

int always_use_pow;
module_param(always_use_pow, int, 0444);
MODULE_PARM_DESC(always_use_pow, "\n"
	"\tWhen set, always send to the pow group. This will cause\n"
	"\tpackets sent to real ethernet devices to be sent to the\n"
	"\tPOW group instead of the hardware. Unless some other\n"
	"\tapplication changes the config, packets will still be\n"
	"\treceived from the low level hardware. Use this option\n"
	"\tto allow a CVMX app to intercept all packets from the\n"
	"\tlinux kernel. You must specify pow_send_group along with\n"
	"\tthis option.");

char pow_send_list[128] = "";
module_param_string(pow_send_list, pow_send_list, sizeof(pow_send_list), 0444);
MODULE_PARM_DESC(pow_send_list, "\n"
	"\tComma separated list of ethernet devices that should use the\n"
	"\tPOW for transmit instead of the actual ethernet hardware. This\n"
	"\tis a per port version of always_use_pow. always_use_pow takes\n"
	"\tprecedence over this list. For example, setting this to\n"
	"\t\"eth2,spi3,spi7\" would cause these three devices to transmit\n"
	"\tusing the pow_send_group.");

static int disable_core_queueing = 1;
module_param(disable_core_queueing, int, 0444);
MODULE_PARM_DESC(disable_core_queueing, "\n"
	"\tWhen set the networking core's tx_queue_len is set to zero.  This\n"
	"\tallows packets to be sent without lock contention in the packet\n"
	"\tscheduler resulting in some cases in improved throughput.\n");

/**
 * Periodic timer to check auto negotiation
 */
static struct timer_list cvm_oct_poll_timer;

/**
 * Array of every ethernet device owned by this driver indexed by
 * the ipd input port number.
 */
struct net_device *cvm_oct_device[TOTAL_NUMBER_OF_PORTS];

extern struct semaphore mdio_sem;

/**
 * Periodic timer tick for slow management operations
 *
 * @arg:    Device to check
 */
static void cvm_do_timer(unsigned long arg)
{
	static int port;
	if (port < CVMX_PIP_NUM_INPUT_PORTS) {
		if (cvm_oct_device[port]) {
			int queues_per_port;
			int qos;
			struct octeon_ethernet *priv =
				netdev_priv(cvm_oct_device[port]);
			if (priv->poll) {
				/* skip polling if we don't get the lock */
				if (!down_trylock(&mdio_sem)) {
					priv->poll(cvm_oct_device[port]);
					up(&mdio_sem);
				}
			}

			queues_per_port = cvmx_pko_get_num_queues(port);
			/* Drain any pending packets in the free list */
			for (qos = 0; qos < queues_per_port; qos++) {
				if (skb_queue_len(&priv->tx_free_list[qos])) {
					spin_lock(&priv->tx_free_list[qos].
						  lock);
					while (skb_queue_len
					       (&priv->tx_free_list[qos]) >
					       cvmx_fau_fetch_and_add32(priv->
									fau +
									qos * 4,
									0))
						dev_kfree_skb(__skb_dequeue
							      (&priv->
							       tx_free_list
							       [qos]));
					spin_unlock(&priv->tx_free_list[qos].
						    lock);
				}
			}
			cvm_oct_device[port]->get_stats(cvm_oct_device[port]);
		}
		port++;
		/* Poll the next port in a 50th of a second.
		   This spreads the polling of ports out a little bit */
		mod_timer(&cvm_oct_poll_timer, jiffies + HZ / 50);
	} else {
		port = 0;
		/* All ports have been polled. Start the next iteration through
		   the ports in one second */
		mod_timer(&cvm_oct_poll_timer, jiffies + HZ);
	}
}

/**
 * Configure common hardware for all interfaces
 */
static __init void cvm_oct_configure_common_hw(void)
{
	int r;
	/* Setup the FPA */
	cvmx_fpa_enable();
	cvm_oct_mem_fill_fpa(CVMX_FPA_PACKET_POOL, CVMX_FPA_PACKET_POOL_SIZE,
			     num_packet_buffers);
	cvm_oct_mem_fill_fpa(CVMX_FPA_WQE_POOL, CVMX_FPA_WQE_POOL_SIZE,
			     num_packet_buffers);
	if (CVMX_FPA_OUTPUT_BUFFER_POOL != CVMX_FPA_PACKET_POOL)
		cvm_oct_mem_fill_fpa(CVMX_FPA_OUTPUT_BUFFER_POOL,
				     CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE, 128);

	if (USE_RED)
		cvmx_helper_setup_red(num_packet_buffers / 4,
				      num_packet_buffers / 8);

	/* Enable the MII interface */
	if (!octeon_is_simulation())
		cvmx_write_csr(CVMX_SMIX_EN(0), 1);

	/* Register an IRQ hander for to receive POW interrupts */
	r = request_irq(OCTEON_IRQ_WORKQ0 + pow_receive_group,
			cvm_oct_do_interrupt, IRQF_SHARED, "Ethernet",
			cvm_oct_device);

#if defined(CONFIG_SMP) && 0
	if (USE_MULTICORE_RECEIVE) {
		irq_set_affinity(OCTEON_IRQ_WORKQ0 + pow_receive_group,
				 cpu_online_mask);
	}
#endif
}

/**
 * Free a work queue entry received in a intercept callback.
 *
 * @work_queue_entry:
 *               Work queue entry to free
 * Returns Zero on success, Negative on failure.
 */
int cvm_oct_free_work(void *work_queue_entry)
{
	cvmx_wqe_t *work = work_queue_entry;

	int segments = work->word2.s.bufs;
	union cvmx_buf_ptr segment_ptr = work->packet_ptr;

	while (segments--) {
		union cvmx_buf_ptr next_ptr = *(union cvmx_buf_ptr *)
			cvmx_phys_to_ptr(segment_ptr.s.addr - 8);
		if (unlikely(!segment_ptr.s.i))
			cvmx_fpa_free(cvm_oct_get_buffer_ptr(segment_ptr),
				      segment_ptr.s.pool,
				      DONT_WRITEBACK(CVMX_FPA_PACKET_POOL_SIZE /
						     128));
		segment_ptr = next_ptr;
	}
	cvmx_fpa_free(work, CVMX_FPA_WQE_POOL, DONT_WRITEBACK(1));

	return 0;
}
EXPORT_SYMBOL(cvm_oct_free_work);

/**
 * Module/ driver initialization. Creates the linux network
 * devices.
 *
 * Returns Zero on success
 */
static int __init cvm_oct_init_module(void)
{
	int num_interfaces;
	int interface;
	int fau = FAU_NUM_PACKET_BUFFERS_TO_FREE;
	int qos;

	pr_notice("cavium-ethernet %s\n", OCTEON_ETHERNET_VERSION);

	cvm_oct_proc_initialize();
	cvm_oct_rx_initialize();
	cvm_oct_configure_common_hw();

	cvmx_helper_initialize_packet_io_global();

	/* Change the input group for all ports before input is enabled */
	num_interfaces = cvmx_helper_get_number_of_interfaces();
	for (interface = 0; interface < num_interfaces; interface++) {
		int num_ports = cvmx_helper_ports_on_interface(interface);
		int port;

		for (port = cvmx_helper_get_ipd_port(interface, 0);
		     port < cvmx_helper_get_ipd_port(interface, num_ports);
		     port++) {
			union cvmx_pip_prt_tagx pip_prt_tagx;
			pip_prt_tagx.u64 =
			    cvmx_read_csr(CVMX_PIP_PRT_TAGX(port));
			pip_prt_tagx.s.grp = pow_receive_group;
			cvmx_write_csr(CVMX_PIP_PRT_TAGX(port),
				       pip_prt_tagx.u64);
		}
	}

	cvmx_helper_ipd_and_packet_input_enable();

	memset(cvm_oct_device, 0, sizeof(cvm_oct_device));

	/*
	 * Initialize the FAU used for counting packet buffers that
	 * need to be freed.
	 */
	cvmx_fau_atomic_write32(FAU_NUM_PACKET_BUFFERS_TO_FREE, 0);

	if ((pow_send_group != -1)) {
		struct net_device *dev;
		pr_info("\tConfiguring device for POW only access\n");
		dev = alloc_etherdev(sizeof(struct octeon_ethernet));
		if (dev) {
			/* Initialize the device private structure. */
			struct octeon_ethernet *priv = netdev_priv(dev);
			memset(priv, 0, sizeof(struct octeon_ethernet));

			dev->init = cvm_oct_common_init;
			priv->imode = CVMX_HELPER_INTERFACE_MODE_DISABLED;
			priv->port = CVMX_PIP_NUM_INPUT_PORTS;
			priv->queue = -1;
			strcpy(dev->name, "pow%d");
			for (qos = 0; qos < 16; qos++)
				skb_queue_head_init(&priv->tx_free_list[qos]);

			if (register_netdev(dev) < 0) {
				pr_err("Failed to register ethernet "
					 "device for POW\n");
				kfree(dev);
			} else {
				cvm_oct_device[CVMX_PIP_NUM_INPUT_PORTS] = dev;
				pr_info("%s: POW send group %d, receive "
					"group %d\n",
				     dev->name, pow_send_group,
				     pow_receive_group);
			}
		} else {
			pr_err("Failed to allocate ethernet device "
				 "for POW\n");
		}
	}

	num_interfaces = cvmx_helper_get_number_of_interfaces();
	for (interface = 0; interface < num_interfaces; interface++) {
		cvmx_helper_interface_mode_t imode =
		    cvmx_helper_interface_get_mode(interface);
		int num_ports = cvmx_helper_ports_on_interface(interface);
		int port;

		for (port = cvmx_helper_get_ipd_port(interface, 0);
		     port < cvmx_helper_get_ipd_port(interface, num_ports);
		     port++) {
			struct octeon_ethernet *priv;
			struct net_device *dev =
			    alloc_etherdev(sizeof(struct octeon_ethernet));
			if (!dev) {
				pr_err("Failed to allocate ethernet device "
					 "for port %d\n", port);
				continue;
			}
			if (disable_core_queueing)
				dev->tx_queue_len = 0;

			/* Initialize the device private structure. */
			priv = netdev_priv(dev);
			memset(priv, 0, sizeof(struct octeon_ethernet));

			priv->imode = imode;
			priv->port = port;
			priv->queue = cvmx_pko_get_base_queue(priv->port);
			priv->fau = fau - cvmx_pko_get_num_queues(port) * 4;
			for (qos = 0; qos < 16; qos++)
				skb_queue_head_init(&priv->tx_free_list[qos]);
			for (qos = 0; qos < cvmx_pko_get_num_queues(port);
			     qos++)
				cvmx_fau_atomic_write32(priv->fau + qos * 4, 0);

			switch (priv->imode) {

			/* These types don't support ports to IPD/PKO */
			case CVMX_HELPER_INTERFACE_MODE_DISABLED:
			case CVMX_HELPER_INTERFACE_MODE_PCIE:
			case CVMX_HELPER_INTERFACE_MODE_PICMG:
				break;

			case CVMX_HELPER_INTERFACE_MODE_NPI:
				dev->init = cvm_oct_common_init;
				dev->uninit = cvm_oct_common_uninit;
				strcpy(dev->name, "npi%d");
				break;

			case CVMX_HELPER_INTERFACE_MODE_XAUI:
				dev->init = cvm_oct_xaui_init;
				dev->uninit = cvm_oct_xaui_uninit;
				strcpy(dev->name, "xaui%d");
				break;

			case CVMX_HELPER_INTERFACE_MODE_LOOP:
				dev->init = cvm_oct_common_init;
				dev->uninit = cvm_oct_common_uninit;
				strcpy(dev->name, "loop%d");
				break;

			case CVMX_HELPER_INTERFACE_MODE_SGMII:
				dev->init = cvm_oct_sgmii_init;
				dev->uninit = cvm_oct_sgmii_uninit;
				strcpy(dev->name, "eth%d");
				break;

			case CVMX_HELPER_INTERFACE_MODE_SPI:
				dev->init = cvm_oct_spi_init;
				dev->uninit = cvm_oct_spi_uninit;
				strcpy(dev->name, "spi%d");
				break;

			case CVMX_HELPER_INTERFACE_MODE_RGMII:
			case CVMX_HELPER_INTERFACE_MODE_GMII:
				dev->init = cvm_oct_rgmii_init;
				dev->uninit = cvm_oct_rgmii_uninit;
				strcpy(dev->name, "eth%d");
				break;
			}

			if (!dev->init) {
				kfree(dev);
			} else if (register_netdev(dev) < 0) {
				pr_err("Failed to register ethernet device "
					 "for interface %d, port %d\n",
					 interface, priv->port);
				kfree(dev);
			} else {
				cvm_oct_device[priv->port] = dev;
				fau -=
				    cvmx_pko_get_num_queues(priv->port) *
				    sizeof(uint32_t);
			}
		}
	}

	if (INTERRUPT_LIMIT) {
		/*
		 * Set the POW timer rate to give an interrupt at most
		 * INTERRUPT_LIMIT times per second.
		 */
		cvmx_write_csr(CVMX_POW_WQ_INT_PC,
			       octeon_bootinfo->eclock_hz / (INTERRUPT_LIMIT *
							     16 * 256) << 8);

		/*
		 * Enable POW timer interrupt. It will count when
		 * there are packets available.
		 */
		cvmx_write_csr(CVMX_POW_WQ_INT_THRX(pow_receive_group),
			       0x1ful << 24);
	} else {
		/* Enable POW interrupt when our port has at least one packet */
		cvmx_write_csr(CVMX_POW_WQ_INT_THRX(pow_receive_group), 0x1001);
	}

	/* Enable the poll timer for checking RGMII status */
	init_timer(&cvm_oct_poll_timer);
	cvm_oct_poll_timer.data = 0;
	cvm_oct_poll_timer.function = cvm_do_timer;
	mod_timer(&cvm_oct_poll_timer, jiffies + HZ);

	return 0;
}

/**
 * Module / driver shutdown
 *
 * Returns Zero on success
 */
static void __exit cvm_oct_cleanup_module(void)
{
	int port;

	/* Disable POW interrupt */
	cvmx_write_csr(CVMX_POW_WQ_INT_THRX(pow_receive_group), 0);

	cvmx_ipd_disable();

	/* Free the interrupt handler */
	free_irq(OCTEON_IRQ_WORKQ0 + pow_receive_group, cvm_oct_device);

	del_timer(&cvm_oct_poll_timer);
	cvm_oct_rx_shutdown();
	cvmx_pko_disable();

	/* Free the ethernet devices */
	for (port = 0; port < TOTAL_NUMBER_OF_PORTS; port++) {
		if (cvm_oct_device[port]) {
			cvm_oct_tx_shutdown(cvm_oct_device[port]);
			unregister_netdev(cvm_oct_device[port]);
			kfree(cvm_oct_device[port]);
			cvm_oct_device[port] = NULL;
		}
	}

	cvmx_pko_shutdown();
	cvm_oct_proc_shutdown();

	cvmx_ipd_free_ptr();

	/* Free the HW pools */
	cvm_oct_mem_empty_fpa(CVMX_FPA_PACKET_POOL, CVMX_FPA_PACKET_POOL_SIZE,
			      num_packet_buffers);
	cvm_oct_mem_empty_fpa(CVMX_FPA_WQE_POOL, CVMX_FPA_WQE_POOL_SIZE,
			      num_packet_buffers);
	if (CVMX_FPA_OUTPUT_BUFFER_POOL != CVMX_FPA_PACKET_POOL)
		cvm_oct_mem_empty_fpa(CVMX_FPA_OUTPUT_BUFFER_POOL,
				      CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE, 128);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cavium Networks <support@caviumnetworks.com>");
MODULE_DESCRIPTION("Cavium Networks Octeon ethernet driver.");
module_init(cvm_oct_init_module);
module_exit(cvm_oct_cleanup_module);
