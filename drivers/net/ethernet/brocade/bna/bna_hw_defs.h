/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */

/* File for interrupt macros and functions */

#ifndef __BNA_HW_DEFS_H__
#define __BNA_HW_DEFS_H__

#include "bfi_reg.h"

/* SW imposed limits */

#define BFI_ENET_DEF_TXQ		1
#define BFI_ENET_DEF_RXP		1
#define BFI_ENET_DEF_UCAM		1
#define BFI_ENET_DEF_RITSZ		1

#define BFI_ENET_MAX_MCAM		256

#define BFI_INVALID_RID			-1

#define BFI_IBIDX_SIZE			4

#define BFI_VLAN_WORD_SHIFT		5	/* 32 bits */
#define BFI_VLAN_WORD_MASK		0x1F
#define BFI_VLAN_BLOCK_SHIFT		9	/* 512 bits */
#define BFI_VLAN_BMASK_ALL		0xFF

#define BFI_COALESCING_TIMER_UNIT	5	/* 5us */
#define BFI_MAX_COALESCING_TIMEO	0xFF	/* in 5us units */
#define BFI_MAX_INTERPKT_COUNT		0xFF
#define BFI_MAX_INTERPKT_TIMEO		0xF	/* in 0.5us units */
#define BFI_TX_COALESCING_TIMEO		20	/* 20 * 5 = 100us */
#define BFI_TX_INTERPKT_COUNT		12	/* Pkt Cnt = 12 */
#define BFI_TX_INTERPKT_TIMEO		15	/* 15 * 0.5 = 7.5us */
#define	BFI_RX_COALESCING_TIMEO		12	/* 12 * 5 = 60us */
#define	BFI_RX_INTERPKT_COUNT		6	/* Pkt Cnt = 6 */
#define	BFI_RX_INTERPKT_TIMEO		3	/* 3 * 0.5 = 1.5us */

#define BFI_TXQ_WI_SIZE			64	/* bytes */
#define BFI_RXQ_WI_SIZE			8	/* bytes */
#define BFI_CQ_WI_SIZE			16	/* bytes */
#define BFI_TX_MAX_WRR_QUOTA		0xFFF

#define BFI_TX_MAX_VECTORS_PER_WI	4
#define BFI_TX_MAX_VECTORS_PER_PKT	0xFF
#define BFI_TX_MAX_DATA_PER_VECTOR	0xFFFF
#define BFI_TX_MAX_DATA_PER_PKT		0xFFFFFF

/* Small Q buffer size */
#define BFI_SMALL_RXBUF_SIZE		128

#define BFI_TX_MAX_PRIO			8
#define BFI_TX_PRIO_MAP_ALL		0xFF

/*
 *
 * Register definitions and macros
 *
 */

#define BNA_PCI_REG_CT_ADDRSZ		(0x40000)

#define ct_reg_addr_init(_bna, _pcidev)					\
{									\
	struct bna_reg_offset reg_offset[] =				\
	{{HOSTFN0_INT_STATUS, HOSTFN0_INT_MSK},				\
	 {HOSTFN1_INT_STATUS, HOSTFN1_INT_MSK},				\
	 {HOSTFN2_INT_STATUS, HOSTFN2_INT_MSK},				\
	 {HOSTFN3_INT_STATUS, HOSTFN3_INT_MSK} };			\
									\
	(_bna)->regs.fn_int_status = (_pcidev)->pci_bar_kva +		\
				reg_offset[(_pcidev)->pci_func].fn_int_status;\
	(_bna)->regs.fn_int_mask = (_pcidev)->pci_bar_kva +		\
				reg_offset[(_pcidev)->pci_func].fn_int_mask;\
}

#define ct_bit_defn_init(_bna, _pcidev)					\
{									\
	(_bna)->bits.mbox_status_bits = (__HFN_INT_MBOX_LPU0 |		\
					__HFN_INT_MBOX_LPU1);		\
	(_bna)->bits.mbox_mask_bits = (__HFN_INT_MBOX_LPU0 |		\
					__HFN_INT_MBOX_LPU1);		\
	(_bna)->bits.error_status_bits = (__HFN_INT_ERR_MASK);		\
	(_bna)->bits.error_mask_bits = (__HFN_INT_ERR_MASK);		\
	(_bna)->bits.halt_status_bits = __HFN_INT_LL_HALT;		\
	(_bna)->bits.halt_mask_bits = __HFN_INT_LL_HALT;		\
}

#define ct2_reg_addr_init(_bna, _pcidev)				\
{									\
	(_bna)->regs.fn_int_status = (_pcidev)->pci_bar_kva +		\
				CT2_HOSTFN_INT_STATUS;			\
	(_bna)->regs.fn_int_mask = (_pcidev)->pci_bar_kva +		\
				CT2_HOSTFN_INTR_MASK;			\
}

#define ct2_bit_defn_init(_bna, _pcidev)				\
{									\
	(_bna)->bits.mbox_status_bits = (__HFN_INT_MBOX_LPU0_CT2 |	\
					__HFN_INT_MBOX_LPU1_CT2);	\
	(_bna)->bits.mbox_mask_bits = (__HFN_INT_MBOX_LPU0_CT2 |	\
					__HFN_INT_MBOX_LPU1_CT2);	\
	(_bna)->bits.error_status_bits = (__HFN_INT_ERR_MASK_CT2);	\
	(_bna)->bits.error_mask_bits = (__HFN_INT_ERR_MASK_CT2);	\
	(_bna)->bits.halt_status_bits = __HFN_INT_CPQ_HALT_CT2;		\
	(_bna)->bits.halt_mask_bits = __HFN_INT_CPQ_HALT_CT2;		\
}

#define bna_reg_addr_init(_bna, _pcidev)				\
{									\
	switch ((_pcidev)->device_id) {					\
	case PCI_DEVICE_ID_BROCADE_CT:					\
		ct_reg_addr_init((_bna), (_pcidev));			\
		ct_bit_defn_init((_bna), (_pcidev));			\
		break;							\
	case BFA_PCI_DEVICE_ID_CT2:					\
		ct2_reg_addr_init((_bna), (_pcidev));			\
		ct2_bit_defn_init((_bna), (_pcidev));			\
		break;							\
	}								\
}

#define bna_port_id_get(_bna) ((_bna)->ioceth.ioc.port_id)

/*  Interrupt related bits, flags and macros  */

#define IB_STATUS_BITS		0x0000ffff

#define BNA_IS_MBOX_INTR(_bna, _intr_status)				\
	((_intr_status) & (_bna)->bits.mbox_status_bits)

#define BNA_IS_HALT_INTR(_bna, _intr_status)				\
	((_intr_status) & (_bna)->bits.halt_status_bits)

#define BNA_IS_ERR_INTR(_bna, _intr_status)	\
	((_intr_status) & (_bna)->bits.error_status_bits)

#define BNA_IS_MBOX_ERR_INTR(_bna, _intr_status)	\
	(BNA_IS_MBOX_INTR(_bna, _intr_status) |		\
	BNA_IS_ERR_INTR(_bna, _intr_status))

#define BNA_IS_INTX_DATA_INTR(_intr_status)		\
		((_intr_status) & IB_STATUS_BITS)

#define bna_halt_clear(_bna)						\
do {									\
	u32 init_halt;						\
	init_halt = readl((_bna)->ioceth.ioc.ioc_regs.ll_halt);	\
	init_halt &= ~__FW_INIT_HALT_P;					\
	writel(init_halt, (_bna)->ioceth.ioc.ioc_regs.ll_halt);	\
	init_halt = readl((_bna)->ioceth.ioc.ioc_regs.ll_halt);	\
} while (0)

#define bna_intx_disable(_bna, _cur_mask)				\
{									\
	(_cur_mask) = readl((_bna)->regs.fn_int_mask);		\
	writel(0xffffffff, (_bna)->regs.fn_int_mask);		\
}

#define bna_intx_enable(bna, new_mask)					\
	writel((new_mask), (bna)->regs.fn_int_mask)
#define bna_mbox_intr_disable(bna)					\
do {									\
	u32 mask;							\
	mask = readl((bna)->regs.fn_int_mask);				\
	writel((mask | (bna)->bits.mbox_mask_bits |			\
		(bna)->bits.error_mask_bits), (bna)->regs.fn_int_mask); \
	mask = readl((bna)->regs.fn_int_mask);				\
} while (0)

#define bna_mbox_intr_enable(bna)					\
do {									\
	u32 mask;							\
	mask = readl((bna)->regs.fn_int_mask);				\
	writel((mask & ~((bna)->bits.mbox_mask_bits |			\
		(bna)->bits.error_mask_bits)), (bna)->regs.fn_int_mask);\
	mask = readl((bna)->regs.fn_int_mask);				\
} while (0)

#define bna_intr_status_get(_bna, _status)				\
{									\
	(_status) = readl((_bna)->regs.fn_int_status);			\
	if (_status) {							\
		writel(((_status) & ~(_bna)->bits.mbox_status_bits),	\
			(_bna)->regs.fn_int_status);			\
	}								\
}

/*
 * MAX ACK EVENTS : No. of acks that can be accumulated in driver,
 * before acking to h/w. The no. of bits is 16 in the doorbell register,
 * however we keep this limited to 15 bits.
 * This is because around the edge of 64K boundary (16 bits), one
 * single poll can make the accumulated ACK counter cross the 64K boundary,
 * causing problems, when we try to ack with a value greater than 64K.
 * 15 bits (32K) should  be large enough to accumulate, anyways, and the max.
 * acked events to h/w can be (32K + max poll weight) (currently 64).
 */
#define	BNA_IB_MAX_ACK_EVENTS		(1 << 15)

/* These macros build the data portion of the TxQ/RxQ doorbell */
#define BNA_DOORBELL_Q_PRD_IDX(_pi)	(0x80000000 | (_pi))
#define BNA_DOORBELL_Q_STOP		(0x40000000)

/* These macros build the data portion of the IB doorbell */
#define BNA_DOORBELL_IB_INT_ACK(_timeout, _events)			\
	(0x80000000 | ((_timeout) << 16) | (_events))
#define BNA_DOORBELL_IB_INT_DISABLE	(0x40000000)

/* Set the coalescing timer for the given ib */
#define bna_ib_coalescing_timer_set(_i_dbell, _cls_timer)		\
	((_i_dbell)->doorbell_ack = BNA_DOORBELL_IB_INT_ACK((_cls_timer), 0));

/* Acks 'events' # of events for a given ib while disabling interrupts */
#define bna_ib_ack_disable_irq(_i_dbell, _events)			\
	(writel(BNA_DOORBELL_IB_INT_ACK(0, (_events)), \
		(_i_dbell)->doorbell_addr));

/* Acks 'events' # of events for a given ib */
#define bna_ib_ack(_i_dbell, _events)					\
	(writel(((_i_dbell)->doorbell_ack | (_events)), \
		(_i_dbell)->doorbell_addr));

#define bna_ib_start(_bna, _ib, _is_regular)				\
{									\
	u32 intx_mask;						\
	struct bna_ib *ib = _ib;					\
	if ((ib->intr_type == BNA_INTR_T_INTX)) {			\
		bna_intx_disable((_bna), intx_mask);			\
		intx_mask &= ~(ib->intr_vector);			\
		bna_intx_enable((_bna), intx_mask);			\
	}								\
	bna_ib_coalescing_timer_set(&ib->door_bell,			\
			ib->coalescing_timeo);				\
	if (_is_regular)						\
		bna_ib_ack(&ib->door_bell, 0);				\
}

#define bna_ib_stop(_bna, _ib)						\
{									\
	u32 intx_mask;						\
	struct bna_ib *ib = _ib;					\
	writel(BNA_DOORBELL_IB_INT_DISABLE,				\
		ib->door_bell.doorbell_addr);				\
	if (ib->intr_type == BNA_INTR_T_INTX) {				\
		bna_intx_disable((_bna), intx_mask);			\
		intx_mask |= ib->intr_vector;				\
		bna_intx_enable((_bna), intx_mask);			\
	}								\
}

#define bna_txq_prod_indx_doorbell(_tcb)				\
	(writel(BNA_DOORBELL_Q_PRD_IDX((_tcb)->producer_index), \
		(_tcb)->q_dbell));

#define bna_rxq_prod_indx_doorbell(_rcb)				\
	(writel(BNA_DOORBELL_Q_PRD_IDX((_rcb)->producer_index), \
		(_rcb)->q_dbell));

/* TxQ, RxQ, CQ related bits, offsets, macros */

/* TxQ Entry Opcodes */
#define BNA_TXQ_WI_SEND			(0x402)	/* Single Frame Transmission */
#define BNA_TXQ_WI_SEND_LSO		(0x403)	/* Multi-Frame Transmission */
#define BNA_TXQ_WI_EXTENSION		(0x104)	/* Extension WI */

/* TxQ Entry Control Flags */
#define BNA_TXQ_WI_CF_FCOE_CRC		(1 << 8)
#define BNA_TXQ_WI_CF_IPID_MODE		(1 << 5)
#define BNA_TXQ_WI_CF_INS_PRIO		(1 << 4)
#define BNA_TXQ_WI_CF_INS_VLAN		(1 << 3)
#define BNA_TXQ_WI_CF_UDP_CKSUM		(1 << 2)
#define BNA_TXQ_WI_CF_TCP_CKSUM		(1 << 1)
#define BNA_TXQ_WI_CF_IP_CKSUM		(1 << 0)

#define BNA_TXQ_WI_L4_HDR_N_OFFSET(_hdr_size, _offset) \
		(((_hdr_size) << 10) | ((_offset) & 0x3FF))

/*
 * Completion Q defines
 */
/* CQ Entry Flags */
#define	BNA_CQ_EF_MAC_ERROR	(1 <<  0)
#define	BNA_CQ_EF_FCS_ERROR	(1 <<  1)
#define	BNA_CQ_EF_TOO_LONG	(1 <<  2)
#define	BNA_CQ_EF_FC_CRC_OK	(1 <<  3)

#define	BNA_CQ_EF_RSVD1		(1 <<  4)
#define	BNA_CQ_EF_L4_CKSUM_OK	(1 <<  5)
#define	BNA_CQ_EF_L3_CKSUM_OK	(1 <<  6)
#define	BNA_CQ_EF_HDS_HEADER	(1 <<  7)

#define	BNA_CQ_EF_UDP		(1 <<  8)
#define	BNA_CQ_EF_TCP		(1 <<  9)
#define	BNA_CQ_EF_IP_OPTIONS	(1 << 10)
#define	BNA_CQ_EF_IPV6		(1 << 11)

#define	BNA_CQ_EF_IPV4		(1 << 12)
#define	BNA_CQ_EF_VLAN		(1 << 13)
#define	BNA_CQ_EF_RSS		(1 << 14)
#define	BNA_CQ_EF_RSVD2		(1 << 15)

#define	BNA_CQ_EF_MCAST_MATCH   (1 << 16)
#define	BNA_CQ_EF_MCAST		(1 << 17)
#define BNA_CQ_EF_BCAST		(1 << 18)
#define	BNA_CQ_EF_REMOTE	(1 << 19)

#define	BNA_CQ_EF_LOCAL		(1 << 20)
/* CAT2 ASIC does not use bit 21 as per the SPEC.
 * Bit 31 is set in every end of frame completion
 */
#define BNA_CQ_EF_EOP		(1 << 31)

/* Data structures */

struct bna_reg_offset {
	u32 fn_int_status;
	u32 fn_int_mask;
};

struct bna_bit_defn {
	u32 mbox_status_bits;
	u32 mbox_mask_bits;
	u32 error_status_bits;
	u32 error_mask_bits;
	u32 halt_status_bits;
	u32 halt_mask_bits;
};

struct bna_reg {
	void __iomem *fn_int_status;
	void __iomem *fn_int_mask;
};

/* TxQ Vector (a.k.a. Tx-Buffer Descriptor) */
struct bna_dma_addr {
	u32		msb;
	u32		lsb;
};

struct bna_txq_wi_vector {
	u16		reserved;
	u16		length;		/* Only 14 LSB are valid */
	struct bna_dma_addr host_addr; /* Tx-Buf DMA addr */
};

/*  TxQ Entry Structure
 *
 *  BEWARE:  Load values into this structure with correct endianess.
 */
struct bna_txq_entry {
	union {
		struct {
			u8 reserved;
			u8 num_vectors;	/* number of vectors present */
			u16 opcode; /* Either */
						    /* BNA_TXQ_WI_SEND or */
						    /* BNA_TXQ_WI_SEND_LSO */
			u16 flags; /* OR of all the flags */
			u16 l4_hdr_size_n_offset;
			u16 vlan_tag;
			u16 lso_mss;	/* Only 14 LSB are valid */
			u32 frame_length;	/* Only 24 LSB are valid */
		} wi;

		struct {
			u16 reserved;
			u16 opcode; /* Must be */
						    /* BNA_TXQ_WI_EXTENSION */
			u32 reserved2[3];	/* Place holder for */
						/* removed vector (12 bytes) */
		} wi_ext;
	} hdr;
	struct bna_txq_wi_vector vector[4];
};

/* RxQ Entry Structure */
struct bna_rxq_entry {		/* Rx-Buffer */
	struct bna_dma_addr host_addr; /* Rx-Buffer DMA address */
};

/* CQ Entry Structure */
struct bna_cq_entry {
	u32 flags;
	u16 vlan_tag;
	u16 length;
	u32 rss_hash;
	u8 valid;
	u8 reserved1;
	u8 reserved2;
	u8 rxq_id;
};

#endif /* __BNA_HW_DEFS_H__ */
