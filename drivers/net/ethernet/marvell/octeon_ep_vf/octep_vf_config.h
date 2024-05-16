/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef _OCTEP_VF_CONFIG_H_
#define _OCTEP_VF_CONFIG_H_

/* Tx instruction types by length */
#define OCTEP_VF_32BYTE_INSTR  32
#define OCTEP_VF_64BYTE_INSTR  64

/* Tx Queue: maximum descriptors per ring */
#define OCTEP_VF_IQ_MAX_DESCRIPTORS    1024
/* Minimum input (Tx) requests to be enqueued to ring doorbell */
#define OCTEP_VF_DB_MIN                8
/* Packet threshold for Tx queue interrupt */
#define OCTEP_VF_IQ_INTR_THRESHOLD     0x0

/* Minimum watermark for backpressure */
#define OCTEP_VF_OQ_WMARK_MIN 256

/* Rx Queue: maximum descriptors per ring */
#define OCTEP_VF_OQ_MAX_DESCRIPTORS   1024

/* Rx buffer size: Use page size buffers.
 * Build skb from allocated page buffer once the packet is received.
 * When a gathered packet is received, make head page as skb head and
 * page buffers in consecutive Rx descriptors as fragments.
 */
#define OCTEP_VF_OQ_BUF_SIZE          (SKB_WITH_OVERHEAD(PAGE_SIZE))
#define OCTEP_VF_OQ_PKTS_PER_INTR     128
#define OCTEP_VF_OQ_REFILL_THRESHOLD  (OCTEP_VF_OQ_MAX_DESCRIPTORS / 4)

#define OCTEP_VF_OQ_INTR_PKT_THRESHOLD   1
#define OCTEP_VF_OQ_INTR_TIME_THRESHOLD  10

#define OCTEP_VF_MSIX_NAME_SIZE      (IFNAMSIZ + 32)

/* Tx Queue wake threshold
 * wakeup a stopped Tx queue if minimum 2 descriptors are available.
 * Even a skb with fragments consume only one Tx queue descriptor entry.
 */
#define OCTEP_VF_WAKE_QUEUE_THRESHOLD 2

/* Minimum MTU supported by Octeon network interface */
#define OCTEP_VF_MIN_MTU        ETH_MIN_MTU
/* Maximum MTU supported by Octeon interface*/
#define OCTEP_VF_MAX_MTU        (10000 - (ETH_HLEN + ETH_FCS_LEN))
/* Default MTU */
#define OCTEP_VF_DEFAULT_MTU    1500

/* Macros to get octeon config params */
#define CFG_GET_IQ_CFG(cfg)             ((cfg)->iq)
#define CFG_GET_IQ_NUM_DESC(cfg)        ((cfg)->iq.num_descs)
#define CFG_GET_IQ_INSTR_TYPE(cfg)      ((cfg)->iq.instr_type)
#define CFG_GET_IQ_INSTR_SIZE(cfg)      (64)
#define CFG_GET_IQ_DB_MIN(cfg)          ((cfg)->iq.db_min)
#define CFG_GET_IQ_INTR_THRESHOLD(cfg)  ((cfg)->iq.intr_threshold)

#define CFG_GET_OQ_NUM_DESC(cfg)          ((cfg)->oq.num_descs)
#define CFG_GET_OQ_BUF_SIZE(cfg)          ((cfg)->oq.buf_size)
#define CFG_GET_OQ_REFILL_THRESHOLD(cfg)  ((cfg)->oq.refill_threshold)
#define CFG_GET_OQ_INTR_PKT(cfg)          ((cfg)->oq.oq_intr_pkt)
#define CFG_GET_OQ_INTR_TIME(cfg)         ((cfg)->oq.oq_intr_time)
#define CFG_GET_OQ_WMARK(cfg)             ((cfg)->oq.wmark)

#define CFG_GET_PORTS_ACTIVE_IO_RINGS(cfg) ((cfg)->ring_cfg.active_io_rings)
#define CFG_GET_PORTS_MAX_IO_RINGS(cfg) ((cfg)->ring_cfg.max_io_rings)

#define CFG_GET_CORE_TICS_PER_US(cfg)     ((cfg)->core_cfg.core_tics_per_us)
#define CFG_GET_COPROC_TICS_PER_US(cfg)   ((cfg)->core_cfg.coproc_tics_per_us)

#define CFG_GET_IOQ_MSIX(cfg)            ((cfg)->msix_cfg.ioq_msix)

/* Hardware Tx Queue configuration. */
struct octep_vf_iq_config {
	/* Size of the Input queue (number of commands) */
	u16 num_descs;

	/* Command size - 32 or 64 bytes */
	u16 instr_type;

	/* Minimum number of commands pending to be posted to Octeon before driver
	 * hits the Input queue doorbell.
	 */
	u16 db_min;

	/* Trigger the IQ interrupt when processed cmd count reaches
	 * this level.
	 */
	u32 intr_threshold;
};

/* Hardware Rx Queue configuration. */
struct octep_vf_oq_config {
	/* Size of Output queue (number of descriptors) */
	u16 num_descs;

	/* Size of buffer in this Output queue. */
	u16 buf_size;

	/* The number of buffers that were consumed during packet processing
	 * by the driver on this Output queue before the driver attempts to
	 * replenish the descriptor ring with new buffers.
	 */
	u16 refill_threshold;

	/* Interrupt Coalescing (Packet Count). Octeon will interrupt the host
	 * only if it sent as many packets as specified by this field.
	 * The driver usually does not use packet count interrupt coalescing.
	 */
	u32 oq_intr_pkt;

	/* Interrupt Coalescing (Time Interval). Octeon will interrupt the host
	 * if at least one packet was sent in the time interval specified by
	 * this field. The driver uses time interval interrupt coalescing by
	 * default. The time is specified in microseconds.
	 */
	u32 oq_intr_time;

	/* Water mark for backpressure.
	 * Output queue sends backpressure signal to source when
	 * free buffer count falls below wmark.
	 */
	u32 wmark;
};

/* Tx/Rx configuration */
struct octep_vf_ring_config {
	/* Max number of IOQs */
	u16 max_io_rings;

	/* Number of active IOQs */
	u16 active_io_rings;
};

/* Octeon MSI-x config. */
struct octep_vf_msix_config {
	/* Number of IOQ interrupts */
	u16 ioq_msix;
};

/* Data Structure to hold configuration limits and active config */
struct octep_vf_config {
	/* Input Queue attributes. */
	struct octep_vf_iq_config iq;

	/* Output Queue attributes. */
	struct octep_vf_oq_config oq;

	/* MSI-X interrupt config */
	struct octep_vf_msix_config msix_cfg;

	/* NIC VF ring Configuration */
	struct octep_vf_ring_config ring_cfg;
};
#endif /* _OCTEP_VF_CONFIG_H_ */
