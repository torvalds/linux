/*
 * cxgb4i.h: Chelsio T4 iSCSI driver.
 *
 * Copyright (c) 2010-2015 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 * Written by: Rakesh Ranjan (rranjan@chelsio.com)
 */

#ifndef	__CXGB4I_H__
#define	__CXGB4I_H__

#define	CXGB4I_SCSI_HOST_QDEPTH	1024
#define	CXGB4I_MAX_CONN		16384
#define	CXGB4I_MAX_TARGET	CXGB4I_MAX_CONN
#define	CXGB4I_MAX_LUN		0x1000

/* for TX: a skb must have a headroom of at least TX_HEADER_LEN bytes */
#define CXGB4I_TX_HEADER_LEN \
	(sizeof(struct fw_ofld_tx_data_wr) + sizeof(struct sge_opaque_hdr))

#define T5_ISS_VALID		(1 << 18)

#endif	/* __CXGB4I_H__ */
