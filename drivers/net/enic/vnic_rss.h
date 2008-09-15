/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#ifndef _VNIC_RSS_H_
#define _VNIC_RSS_H_

/* RSS key array */
union vnic_rss_key {
	struct {
		u8 b[10];
		u8 b_pad[6];
	} key[4];
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

void vnic_set_rss_key(union vnic_rss_key *rss_key, u8 *key);
void vnic_set_rss_cpu(union vnic_rss_cpu *rss_cpu, u8 *cpu);
void vnic_get_rss_key(union vnic_rss_key *rss_key, u8 *key);
void vnic_get_rss_cpu(union vnic_rss_cpu *rss_cpu, u8 *cpu);

#endif /* _VNIC_RSS_H_ */
