/*
 * 802.1Q VLAN protocol definitions
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: vlan.h 382883 2013-02-04 23:26:09Z $
 */

#ifndef _vlan_h_
#define _vlan_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif


#include <packed_section_start.h>

#ifndef	 VLAN_VID_MASK
#define VLAN_VID_MASK		0xfff	
#endif

#define	VLAN_CFI_SHIFT		12	
#define VLAN_PRI_SHIFT		13	

#define VLAN_PRI_MASK		7	

#define	VLAN_TPID_OFFSET	12	
#define	VLAN_TCI_OFFSET		14	

#define	VLAN_TAG_LEN		4
#define	VLAN_TAG_OFFSET		(2 * ETHER_ADDR_LEN)	

#define VLAN_TPID		0x8100	

struct vlan_header {
	uint16	vlan_type;		
	uint16	vlan_tag;		
};

struct ethervlan_header {
	uint8	ether_dhost[ETHER_ADDR_LEN];
	uint8	ether_shost[ETHER_ADDR_LEN];
	uint16	vlan_type;		
	uint16	vlan_tag;		
	uint16	ether_type;
};

struct dot3_mac_llc_snapvlan_header {
	uint8	ether_dhost[ETHER_ADDR_LEN];	
	uint8	ether_shost[ETHER_ADDR_LEN];	
	uint16	length;				
	uint8	dsap;				
	uint8	ssap;				
	uint8	ctl;				
	uint8	oui[3];				
	uint16	vlan_type;			
	uint16	vlan_tag;			
	uint16	ether_type;			
};

#define	ETHERVLAN_HDR_LEN	(ETHER_HDR_LEN + VLAN_TAG_LEN)



#include <packed_section_end.h>

#define ETHERVLAN_MOVE_HDR(d, s) \
do { \
	struct ethervlan_header t; \
	t = *(struct ethervlan_header *)(s); \
	*(struct ethervlan_header *)(d) = t; \
} while (0)

#endif 
