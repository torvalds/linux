/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#ifndef _VNIC_RSS_H_
#define _VNIC_RSS_H_

/* RSS key array */

#define ENIC_RSS_BYTES_PER_KEY	10
#define ENIC_RSS_KEYS		4
#define ENIC_RSS_LEN		(ENIC_RSS_BYTES_PER_KEY * ENIC_RSS_KEYS)

union vnic_rss_key {
	struct {
		u8 b[ENIC_RSS_BYTES_PER_KEY];
		u8 b_pad[6];
	} key[ENIC_RSS_KEYS];
	u64 raw[8];
};

/* RSS cpu array */
union vnic_rss_cpu {
	struct {
		u8 b[4] ;
		u8 b_pad[4];
	} cpu[32];
	u64 raw[32];
};

#endif /* _VNIC_RSS_H_ */
