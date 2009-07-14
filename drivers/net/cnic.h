/* cnic.h: Broadcom CNIC core network driver.
 *
 * Copyright (c) 2006-2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 */


#ifndef CNIC_H
#define CNIC_H

#define KWQ_PAGE_CNT	4
#define KCQ_PAGE_CNT	16

#define KWQ_CID 		24
#define KCQ_CID 		25

/*
 *	krnlq_context definition
 */
#define L5_KRNLQ_FLAGS	0x00000000
#define L5_KRNLQ_SIZE	0x00000000
#define L5_KRNLQ_TYPE	0x00000000
#define KRNLQ_FLAGS_PG_SZ					(0xf<<0)
#define KRNLQ_FLAGS_PG_SZ_256					(0<<0)
#define KRNLQ_FLAGS_PG_SZ_512					(1<<0)
#define KRNLQ_FLAGS_PG_SZ_1K					(2<<0)
#define KRNLQ_FLAGS_PG_SZ_2K					(3<<0)
#define KRNLQ_FLAGS_PG_SZ_4K					(4<<0)
#define KRNLQ_FLAGS_PG_SZ_8K					(5<<0)
#define KRNLQ_FLAGS_PG_SZ_16K					(6<<0)
#define KRNLQ_FLAGS_PG_SZ_32K					(7<<0)
#define KRNLQ_FLAGS_PG_SZ_64K					(8<<0)
#define KRNLQ_FLAGS_PG_SZ_128K					(9<<0)
#define KRNLQ_FLAGS_PG_SZ_256K					(10<<0)
#define KRNLQ_FLAGS_PG_SZ_512K					(11<<0)
#define KRNLQ_FLAGS_PG_SZ_1M					(12<<0)
#define KRNLQ_FLAGS_PG_SZ_2M					(13<<0)
#define KRNLQ_FLAGS_QE_SELF_SEQ					(1<<15)
#define KRNLQ_SIZE_TYPE_SIZE	((((0x28 + 0x1f) & ~0x1f) / 0x20) << 16)
#define KRNLQ_TYPE_TYPE						(0xf<<28)
#define KRNLQ_TYPE_TYPE_EMPTY					(0<<28)
#define KRNLQ_TYPE_TYPE_KRNLQ					(6<<28)

#define L5_KRNLQ_HOST_QIDX		0x00000004
#define L5_KRNLQ_HOST_FW_QIDX		0x00000008
#define L5_KRNLQ_NX_QE_SELF_SEQ 	0x0000000c
#define L5_KRNLQ_QE_SELF_SEQ_MAX	0x0000000c
#define L5_KRNLQ_NX_QE_HADDR_HI 	0x00000010
#define L5_KRNLQ_NX_QE_HADDR_LO 	0x00000014
#define L5_KRNLQ_PGTBL_PGIDX		0x00000018
#define L5_KRNLQ_NX_PG_QIDX 		0x00000018
#define L5_KRNLQ_PGTBL_NPAGES		0x0000001c
#define L5_KRNLQ_QIDX_INCR		0x0000001c
#define L5_KRNLQ_PGTBL_HADDR_HI 	0x00000020
#define L5_KRNLQ_PGTBL_HADDR_LO 	0x00000024

#define BNX2_PG_CTX_MAP			0x1a0034
#define BNX2_ISCSI_CTX_MAP		0x1a0074

struct cnic_redirect_entry {
	struct dst_entry *old_dst;
	struct dst_entry *new_dst;
};

#define MAX_COMPLETED_KCQE	64

#define MAX_CNIC_L5_CONTEXT	256

#define MAX_CM_SK_TBL_SZ	MAX_CNIC_L5_CONTEXT

#define MAX_ISCSI_TBL_SZ	256

#define CNIC_LOCAL_PORT_MIN	60000
#define CNIC_LOCAL_PORT_MAX	61000
#define CNIC_LOCAL_PORT_RANGE	(CNIC_LOCAL_PORT_MAX - CNIC_LOCAL_PORT_MIN)

#define KWQE_CNT (BCM_PAGE_SIZE / sizeof(struct kwqe))
#define KCQE_CNT (BCM_PAGE_SIZE / sizeof(struct kcqe))
#define MAX_KWQE_CNT (KWQE_CNT - 1)
#define MAX_KCQE_CNT (KCQE_CNT - 1)

#define MAX_KWQ_IDX	((KWQ_PAGE_CNT * KWQE_CNT) - 1)
#define MAX_KCQ_IDX	((KCQ_PAGE_CNT * KCQE_CNT) - 1)

#define KWQ_PG(x) (((x) & ~MAX_KWQE_CNT) >> (BCM_PAGE_BITS - 5))
#define KWQ_IDX(x) ((x) & MAX_KWQE_CNT)

#define KCQ_PG(x) (((x) & ~MAX_KCQE_CNT) >> (BCM_PAGE_BITS - 5))
#define KCQ_IDX(x) ((x) & MAX_KCQE_CNT)

#define BNX2X_NEXT_KCQE(x) (((x) & (MAX_KCQE_CNT - 1)) ==		\
		(MAX_KCQE_CNT - 1)) ?					\
		(x) + 2 : (x) + 1

#define BNX2X_KWQ_DATA_PG(cp, x) ((x) / (cp)->kwq_16_data_pp)
#define BNX2X_KWQ_DATA_IDX(cp, x) ((x) % (cp)->kwq_16_data_pp)
#define BNX2X_KWQ_DATA(cp, x)						\
	&(cp)->kwq_16_data[BNX2X_KWQ_DATA_PG(cp, x)][BNX2X_KWQ_DATA_IDX(cp, x)]

#define DEF_IPID_COUNT		0xc001

#define DEF_KA_TIMEOUT		10000
#define DEF_KA_INTERVAL		300000
#define DEF_KA_MAX_PROBE_COUNT	3
#define DEF_TOS			0
#define DEF_TTL			0xfe
#define DEF_SND_SEQ_SCALE	0
#define DEF_RCV_BUF		0xffff
#define DEF_SND_BUF		0xffff
#define DEF_SEED		0
#define DEF_MAX_RT_TIME		500
#define DEF_MAX_DA_COUNT	2
#define DEF_SWS_TIMER		1000
#define DEF_MAX_CWND		0xffff

struct cnic_ctx {
	u32		cid;
	void		*ctx;
	dma_addr_t	mapping;
};

#define BNX2_MAX_CID		0x2000

struct cnic_dma {
	int		num_pages;
	void		**pg_arr;
	dma_addr_t	*pg_map_arr;
	int		pgtbl_size;
	u32		*pgtbl;
	dma_addr_t	pgtbl_map;
};

struct cnic_id_tbl {
	spinlock_t	lock;
	u32		start;
	u32		max;
	u32		next;
	unsigned long	*table;
};

#define CNIC_KWQ16_DATA_SIZE	128

struct kwqe_16_data {
	u8	data[CNIC_KWQ16_DATA_SIZE];
};

struct cnic_iscsi {
	struct cnic_dma		task_array_info;
	struct cnic_dma		r2tq_info;
	struct cnic_dma		hq_info;
};

struct cnic_context {
	u32			cid;
	struct kwqe_16_data	*kwqe_data;
	dma_addr_t		kwqe_data_mapping;
	wait_queue_head_t	waitq;
	int			wait_cond;
	unsigned long		timestamp;
	u32			ctx_flags;
#define	CTX_FL_OFFLD_START	0x00000001
	u8			ulp_proto_id;
	union {
		struct cnic_iscsi	*iscsi;
	} proto;
};

struct cnic_local {

	spinlock_t cnic_ulp_lock;
	void *ulp_handle[MAX_CNIC_ULP_TYPE];
	unsigned long ulp_flags[MAX_CNIC_ULP_TYPE];
#define ULP_F_INIT	0
#define ULP_F_START	1
	struct cnic_ulp_ops *ulp_ops[MAX_CNIC_ULP_TYPE];

	/* protected by ulp_lock */
	u32 cnic_local_flags;
#define	CNIC_LCL_FL_KWQ_INIT	0x00000001

	struct cnic_dev *dev;

	struct cnic_eth_dev *ethdev;

	void		*l2_ring;
	dma_addr_t	l2_ring_map;
	int		l2_ring_size;
	int		l2_rx_ring_size;

	void		*l2_buf;
	dma_addr_t	l2_buf_map;
	int		l2_buf_size;
	int		l2_single_buf_size;

	u16		*rx_cons_ptr;
	u16		*tx_cons_ptr;
	u16		rx_cons;
	u16		tx_cons;

	u32 kwq_cid_addr;
	u32 kcq_cid_addr;

	struct cnic_dma		kwq_info;
	struct kwqe		**kwq;

	struct cnic_dma		kwq_16_data_info;

	u16		max_kwq_idx;

	u16		kwq_prod_idx;
	u32		kwq_io_addr;

	u16		*kwq_con_idx_ptr;
	u16		kwq_con_idx;

	struct cnic_dma	kcq_info;
	struct kcqe	**kcq;

	u16		kcq_prod_idx;
	u32		kcq_io_addr;

	void				*status_blk;
	struct status_block_msix	*bnx2_status_blk;
	struct host_status_block	*bnx2x_status_blk;

	u32				status_blk_num;
	u32				int_num;
	u32				last_status_idx;
	struct tasklet_struct		cnic_irq_task;

	struct kcqe		*completed_kcq[MAX_COMPLETED_KCQE];

	struct cnic_sock	*csk_tbl;
	struct cnic_id_tbl	csk_port_tbl;

	struct cnic_dma		conn_buf_info;
	struct cnic_dma		gbl_buf_info;

	struct cnic_iscsi	*iscsi_tbl;
	struct cnic_context	*ctx_tbl;
	struct cnic_id_tbl	cid_tbl;
	int			max_iscsi_conn;
	atomic_t		iscsi_conn;

	/* per connection parameters */
	int			num_iscsi_tasks;
	int			num_ccells;
	int			task_array_size;
	int			r2tq_size;
	int			hq_size;
	int			num_cqs;

	struct cnic_ctx		*ctx_arr;
	int			ctx_blks;
	int			ctx_blk_size;
	int			cids_per_blk;

	u32			chip_id;
	int			func;
	u32			shmem_base;

	u32			uio_dev;
	struct uio_info		*cnic_uinfo;

	struct cnic_ops		*cnic_ops;
	int			(*start_hw)(struct cnic_dev *);
	void			(*stop_hw)(struct cnic_dev *);
	void			(*setup_pgtbl)(struct cnic_dev *,
					       struct cnic_dma *);
	int			(*alloc_resc)(struct cnic_dev *);
	void			(*free_resc)(struct cnic_dev *);
	int			(*start_cm)(struct cnic_dev *);
	void			(*stop_cm)(struct cnic_dev *);
	void			(*enable_int)(struct cnic_dev *);
	void			(*disable_int_sync)(struct cnic_dev *);
	void			(*ack_int)(struct cnic_dev *);
	void			(*close_conn)(struct cnic_sock *, u32 opcode);
	u16			(*next_idx)(u16);
	u16			(*hw_idx)(u16);
};

struct bnx2x_bd_chain_next {
	u32	addr_lo;
	u32	addr_hi;
	u8	reserved[8];
};

#define ISCSI_RAMROD_CMD_ID_UPDATE_CONN		(ISCSI_KCQE_OPCODE_UPDATE_CONN)
#define ISCSI_RAMROD_CMD_ID_INIT		(ISCSI_KCQE_OPCODE_INIT)

#define CDU_REGION_NUMBER_XCM_AG 2
#define CDU_REGION_NUMBER_UCM_AG 4

#endif

