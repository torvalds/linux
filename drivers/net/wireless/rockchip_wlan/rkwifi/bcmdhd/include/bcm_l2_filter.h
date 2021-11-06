/*
 * L2 Filter handling functions
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 *
 */
#ifndef _l2_filter_h_
#define _l2_filter_h_

/* Proxy ARP processing return values */
#define PARP_DROP			0
#define PARP_NOP			1
#define PARP_TAKEN			2
/* Adjust for ETHER_HDR_LEN pull in linux
 * which makes pkt nonaligned
 */
#define ALIGN_ADJ_BUFLEN		2

#define	BCM_PARP_TABLE_SIZE		32		/* proxyarp hash table bucket size */
#define	BCM_PARP_TABLE_MASK		0x1f	/* proxyarp hash table index mask */
#define	BCM_PARP_TABLE_INDEX(val)	(val & BCM_PARP_TABLE_MASK)
#define	BCM_PARP_TIMEOUT		600	/* proxyarp cache entry timerout duration(10 min) */

#define BCM_PARP_IS_TIMEOUT(pub_tick, entry)	\
			(pub_tick - entry->used > BCM_PARP_TIMEOUT)

#define	BCM_PARP_ANNOUNCE_WAIT		2	/* proxyarp announce wait duration(2 sec) */

#define BCM_PARP_ANNOUNCE_WAIT_REACH(pub_tick, entry) \
	(pub_tick - entry->used > BCM_PARP_ANNOUNCE_WAIT)

#define BCM_ARP_TABLE_UPDATE_TIMEOUT	100

/* Taken from wlc_tdls.h for block_tdls iovar */
#define TDLS_PAYLOAD_TYPE		2
#define TDLS_PAYLOAD_TYPE_LEN		1

/* TDLS Action Category code */
#define TDLS_ACTION_CATEGORY_CODE	12

typedef struct parp_entry {
	struct parp_entry	*next;
	uint32			used;		/* time stamp */
	struct ether_addr	ea;
	bcm_tlv_t		ip;
} parp_entry_t;

typedef struct arp_table arp_table_t;

extern int bcm_l2_filter_gratuitous_arp(osl_t *osh, void *pktbuf);
extern int bcm_l2_filter_block_ping(osl_t *osh, void *pktbuf);
extern int bcm_l2_filter_get_mac_addr_dhcp_pkt(osl_t *osh, void *pktbuf,
	int ifidx, uint8** addr);

arp_table_t* init_l2_filter_arp_table(osl_t* osh);
void deinit_l2_filter_arp_table(osl_t* osh, arp_table_t* ptable);
int get_pkt_ether_type(osl_t *osh, void *skb, uint8 **data_ptr,
	int *len_ptr, uint16 *et_ptr, bool *snap_ptr);
int get_pkt_ip_type(osl_t *osh, void *pktbuf,
	uint8 **data_ptr, int *len_ptr, uint8 *prot_ptr);
int bcm_l2_filter_parp_addentry(osl_t *osh, arp_table_t* arp_tbl, struct ether_addr *ea,
	uint8 *ip, uint8 ip_ver, bool cached, unsigned int entry_tickcnt);
int bcm_l2_filter_parp_delentry(osl_t *osh, arp_table_t* arp_tbl, struct ether_addr *ea,
	uint8 *ip, uint8 ip_ver, bool cached);
parp_entry_t *bcm_l2_filter_parp_findentry(arp_table_t* arp_tbl, uint8 *ip,
	uint8 ip_ver, bool cached, unsigned int entry_tickcnt);

int bcm_l2_filter_parp_modifyentry(arp_table_t* arp_tbl, struct ether_addr *ea,
	uint8 *ip, uint8 ip_ver, bool cached, unsigned int entry_tickcnt);
extern void bcm_l2_filter_arp_table_update(osl_t *osh, arp_table_t* arp_tbl, bool all,
	uint8 *del_ea, bool periodic, unsigned int tickcnt);

void *bcm_l2_filter_proxyarp_alloc_reply(osl_t* osh, uint16 pktlen, struct ether_addr *src_ea,
	struct ether_addr *dst_ea, uint16 ea_type, bool snap, void **p);
void bcm_l2_filter_parp_get_smac(arp_table_t* ptable, void* smac);
void bcm_l2_filter_parp_get_cmac(arp_table_t* ptable, void* cmac);
void bcm_l2_filter_parp_set_smac(arp_table_t* ptable, void* smac);
void bcm_l2_filter_parp_set_cmac(arp_table_t* ptable, void* cmac);
bcm_tlv_t* parse_nd_options(void *buf, int buflen, uint key);
uint16 calc_checksum(uint8 *src_ipa, uint8 *dst_ipa, uint32 ul_len, uint8 prot, uint8 *ul_data);
extern int bcm_l2_filter_block_tdls(osl_t *osh, void *pktbuf);
#endif /* _l2_filter_h */
