/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef _OCTEP_CONFIG_H_
#define _OCTEP_CONFIG_H_

/* Tx instruction types by length */
#define OCTEP_32BYTE_INSTR  32
#define OCTEP_64BYTE_INSTR  64

/* Tx Queue: maximum descriptors per ring */
/* This needs to be a power of 2 */
#define OCTEP_IQ_MAX_DESCRIPTORS    1024
/* Minimum input (Tx) requests to be enqueued to ring doorbell */
#define OCTEP_DB_MIN                8
/* Packet threshold for Tx queue interrupt */
#define OCTEP_IQ_INTR_THRESHOLD     0x0

/* Minimum watermark for backpressure */
#define OCTEP_OQ_WMARK_MIN 256

/* Rx Queue: maximum descriptors per ring */
#define OCTEP_OQ_MAX_DESCRIPTORS   1024

/* Rx buffer size: Use page size buffers.
 * Build skb from allocated page buffer once the packet is received.
 * When a gathered packet is received, make head page as skb head and
 * page buffers in consecutive Rx descriptors as fragments.
 */
#define OCTEP_OQ_BUF_SIZE          (SKB_WITH_OVERHEAD(PAGE_SIZE))
#define OCTEP_OQ_PKTS_PER_INTR     128
#define OCTEP_OQ_REFILL_THRESHOLD  (OCTEP_OQ_MAX_DESCRIPTORS / 4)

#define OCTEP_OQ_INTR_PKT_THRESHOLD   1
#define OCTEP_OQ_INTR_TIME_THRESHOLD  10

#define OCTEP_MSIX_NAME_SIZE      (IFNAMSIZ + 32)

/* Tx Queue wake threshold
 * wakeup a stopped Tx queue if minimum 2 descriptors are available.
 * Even a skb with fragments consume only one Tx queue descriptor entry.
 */
#define OCTEP_WAKE_QUEUE_THRESHOLD 2

/* Minimum MTU supported by Octeon network interface */
#define OCTEP_MIN_MTU        ETH_MIN_MTU
/* Default MTU */
#define OCTEP_DEFAULT_MTU    1500

/* pf heartbeat interval in milliseconds */
#define OCTEP_DEFAULT_FW_HB_INTERVAL           1000
/* pf heartbeat miss count */
#define OCTEP_DEFAULT_FW_HB_MISS_COUNT         20

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

#define CFG_GET_PORTS_MAX_IO_RINGS(cfg)    ((cfg)->pf_ring_cfg.max_io_rings)
#define CFG_GET_PORTS_ACTIVE_IO_RINGS(cfg) ((cfg)->pf_ring_cfg.active_io_rings)
#define CFG_GET_PORTS_PF_SRN(cfg)          ((cfg)->pf_ring_cfg.srn)

#define CFG_GET_CORE_TICS_PER_US(cfg)     ((cfg)->core_cfg.core_tics_per_us)
#define CFG_GET_COPROC_TICS_PER_US(cfg)   ((cfg)->core_cfg.coproc_tics_per_us)

#define CFG_GET_MAX_VFS(cfg)        ((cfg)->sriov_cfg.max_vfs)
#define CFG_GET_ACTIVE_VFS(cfg)     ((cfg)->sriov_cfg.active_vfs)
#define CFG_GET_MAX_RPVF(cfg)       ((cfg)->sriov_cfg.max_rings_per_vf)
#define CFG_GET_ACTIVE_RPVF(cfg)    ((cfg)->sriov_cfg.active_rings_per_vf)
#define CFG_GET_VF_SRN(cfg)         ((cfg)->sriov_cfg.vf_srn)

#define CFG_GET_IOQ_MSIX(cfg)            ((cfg)->msix_cfg.ioq_msix)
#define CFG_GET_NON_IOQ_MSIX(cfg)        ((cfg)->msix_cfg.non_ioq_msix)
#define CFG_GET_NON_IOQ_MSIX_NAMES(cfg)  ((cfg)->msix_cfg.non_ioq_msix_names)

#define CFG_GET_CTRL_MBOX_MEM_ADDR(cfg)  ((cfg)->ctrl_mbox_cfg.barmem_addr)

/* Hardware Tx Queue configuration. */
struct octep_iq_config {
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
struct octep_oq_config {
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
struct octep_pf_ring_config {
	/* Max number of IOQs */
	u16 max_io_rings;

	/* Number of active IOQs */
	u16 active_io_rings;

	/* Starting IOQ number: this changes based on which PEM is used */
	u16 srn;
};

/* Octeon Hardware SRIOV config */
struct octep_sriov_config {
	/* Max number of VF devices supported */
	u16 max_vfs;

	/* Number of VF devices enabled   */
	u16 active_vfs;

	/* Max number of rings assigned to VF  */
	u8 max_rings_per_vf;

	/* Number of rings enabled per VF */
	u8 active_rings_per_vf;

	/* starting ring number of VF's: ring-0 of VF-0 of the PF */
	u16 vf_srn;
};

/* Octeon MSI-x config. */
struct octep_msix_config {
	/* Number of IOQ interrupts */
	u16 ioq_msix;

	/* Number of Non IOQ interrupts */
	u16 non_ioq_msix;

	/* Names of Non IOQ interrupts */
	char **non_ioq_msix_names;
};

struct octep_ctrl_mbox_config {
	/* Barmem address for control mbox */
	void __iomem *barmem_addr;
};

/* Info from firmware */
struct octep_fw_info {
	/* interface pkind */
	u8 pkind;

	/* front size data */
	u8 fsz;

	/* heartbeat interval in milliseconds */
	u16 hb_interval;

	/* heartbeat miss count */
	u16 hb_miss_count;

	/* reserved */
	u16 reserved1;

	/* supported rx offloads OCTEP_ETH_RX_OFFLOAD_* */
	u16 rx_ol_flags;

	/* supported tx offloads OCTEP_ETH_TX_OFFLOAD_* */
	u16 tx_ol_flags;

	/* reserved */
	u32 reserved_offloads;

	/* extra offload flags */
	u64 ext_ol_flags;

	/* supported features */
	u64 features[2];

	/* reserved */
	u64 reserved2[3];
};

/* Data Structure to hold configuration limits and active config */
struct octep_config {
	/* Input Queue attributes. */
	struct octep_iq_config iq;

	/* Output Queue attributes. */
	struct octep_oq_config oq;

	/* NIC Port Configuration */
	struct octep_pf_ring_config pf_ring_cfg;

	/* SRIOV configuration of the PF */
	struct octep_sriov_config sriov_cfg;

	/* MSI-X interrupt config */
	struct octep_msix_config msix_cfg;

	/* ctrl mbox config */
	struct octep_ctrl_mbox_config ctrl_mbox_cfg;

	/* fw info */
	struct octep_fw_info fw_info;
};
#endif /* _OCTEP_CONFIG_H_ */
