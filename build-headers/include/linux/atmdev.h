/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* atmdev.h - ATM device driver declarations and various related items */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
 

#ifndef LINUX_ATMDEV_H
#define LINUX_ATMDEV_H


#include <linux/atmapi.h>
#include <linux/atm.h>
#include <linux/atmioc.h>


#define ESI_LEN		6

#define ATM_OC3_PCR	(155520000/270*260/8/53)
			/* OC3 link rate:  155520000 bps
			   SONET overhead: /270*260 (9 section, 1 path)
			   bits per cell:  /8/53
			   max cell rate:  353207.547 cells/sec */
#define ATM_25_PCR	((25600000/8-8000)/54)
			/* 25 Mbps ATM cell rate (59111) */
#define ATM_OC12_PCR	(622080000/1080*1040/8/53)
			/* OC12 link rate: 622080000 bps
			   SONET overhead: /1080*1040
			   bits per cell:  /8/53
			   max cell rate:  1412830.188 cells/sec */
#define ATM_DS3_PCR	(8000*12)
			/* DS3: 12 cells in a 125 usec time slot */


#define __AAL_STAT_ITEMS \
    __HANDLE_ITEM(tx);			/* TX okay */ \
    __HANDLE_ITEM(tx_err);		/* TX errors */ \
    __HANDLE_ITEM(rx);			/* RX okay */ \
    __HANDLE_ITEM(rx_err);		/* RX errors */ \
    __HANDLE_ITEM(rx_drop);		/* RX out of memory */

struct atm_aal_stats {
#define __HANDLE_ITEM(i) int i
	__AAL_STAT_ITEMS
#undef __HANDLE_ITEM
};


struct atm_dev_stats {
	struct atm_aal_stats aal0;
	struct atm_aal_stats aal34;
	struct atm_aal_stats aal5;
} __ATM_API_ALIGN;


#define ATM_GETLINKRATE	_IOW('a',ATMIOC_ITF+1,struct atmif_sioc)
					/* get link rate */
#define ATM_GETNAMES	_IOW('a',ATMIOC_ITF+3,struct atm_iobuf)
					/* get interface names (numbers) */
#define ATM_GETTYPE	_IOW('a',ATMIOC_ITF+4,struct atmif_sioc)
					/* get interface type name */
#define ATM_GETESI	_IOW('a',ATMIOC_ITF+5,struct atmif_sioc)
					/* get interface ESI */
#define ATM_GETADDR	_IOW('a',ATMIOC_ITF+6,struct atmif_sioc)
					/* get itf's local ATM addr. list */
#define ATM_RSTADDR	_IOW('a',ATMIOC_ITF+7,struct atmif_sioc)
					/* reset itf's ATM address list */
#define ATM_ADDADDR	_IOW('a',ATMIOC_ITF+8,struct atmif_sioc)
					/* add a local ATM address */
#define ATM_DELADDR	_IOW('a',ATMIOC_ITF+9,struct atmif_sioc)
					/* remove a local ATM address */
#define ATM_GETCIRANGE	_IOW('a',ATMIOC_ITF+10,struct atmif_sioc)
					/* get connection identifier range */
#define ATM_SETCIRANGE	_IOW('a',ATMIOC_ITF+11,struct atmif_sioc)
					/* set connection identifier range */
#define ATM_SETESI	_IOW('a',ATMIOC_ITF+12,struct atmif_sioc)
					/* set interface ESI */
#define ATM_SETESIF	_IOW('a',ATMIOC_ITF+13,struct atmif_sioc)
					/* force interface ESI */
#define ATM_ADDLECSADDR	_IOW('a', ATMIOC_ITF+14, struct atmif_sioc)
					/* register a LECS address */
#define ATM_DELLECSADDR	_IOW('a', ATMIOC_ITF+15, struct atmif_sioc)
					/* unregister a LECS address */
#define ATM_GETLECSADDR	_IOW('a', ATMIOC_ITF+16, struct atmif_sioc)
					/* retrieve LECS address(es) */

#define ATM_GETSTAT	_IOW('a',ATMIOC_SARCOM+0,struct atmif_sioc)
					/* get AAL layer statistics */
#define ATM_GETSTATZ	_IOW('a',ATMIOC_SARCOM+1,struct atmif_sioc)
					/* get AAL layer statistics and zero */
#define ATM_GETLOOP	_IOW('a',ATMIOC_SARCOM+2,struct atmif_sioc)
					/* get loopback mode */
#define ATM_SETLOOP	_IOW('a',ATMIOC_SARCOM+3,struct atmif_sioc)
					/* set loopback mode */
#define ATM_QUERYLOOP	_IOW('a',ATMIOC_SARCOM+4,struct atmif_sioc)
					/* query supported loopback modes */
#define ATM_SETSC	_IOW('a',ATMIOC_SPECIAL+1,int)
					/* enable or disable single-copy */
#define ATM_SETBACKEND	_IOW('a',ATMIOC_SPECIAL+2,atm_backend_t)
					/* set backend handler */
#define ATM_NEWBACKENDIF _IOW('a',ATMIOC_SPECIAL+3,atm_backend_t)
					/* use backend to make new if */
#define ATM_ADDPARTY  	_IOW('a', ATMIOC_SPECIAL+4,struct atm_iobuf)
 					/* add party to p2mp call */
#ifdef CONFIG_COMPAT
/* It actually takes struct sockaddr_atmsvc, not struct atm_iobuf */
#define COMPAT_ATM_ADDPARTY  	_IOW('a', ATMIOC_SPECIAL+4,struct compat_atm_iobuf)
#endif
#define ATM_DROPPARTY 	_IOW('a', ATMIOC_SPECIAL+5,int)
					/* drop party from p2mp call */

/*
 * These are backend handkers that can be set via the ATM_SETBACKEND call
 * above.  In the future we may support dynamic loading of these - for now,
 * they're just being used to share the ATMIOC_BACKEND ioctls
 */
#define ATM_BACKEND_RAW		0	
#define ATM_BACKEND_PPP		1	/* PPPoATM - RFC2364 */
#define ATM_BACKEND_BR2684	2	/* Bridged RFC1483/2684 */

/* for ATM_GETTYPE */
#define ATM_ITFTYP_LEN	8	/* maximum length of interface type name */

/*
 * Loopback modes for ATM_{PHY,SAR}_{GET,SET}LOOP
 */

/* Point of loopback				CPU-->SAR-->PHY-->line--> ... */
#define __ATM_LM_NONE	0	/* no loop back     ^     ^     ^      ^      */
#define __ATM_LM_AAL	1	/* loop back PDUs --'     |     |      |      */
#define __ATM_LM_ATM	2	/* loop back ATM cells ---'     |      |      */
/* RESERVED		4	loop back on PHY side  ---'		      */
#define __ATM_LM_PHY	8	/* loop back bits (digital) ----'      |      */
#define __ATM_LM_ANALOG 16	/* loop back the analog signal --------'      */

/* Direction of loopback */
#define __ATM_LM_MKLOC(n)	((n))	    /* Local (i.e. loop TX to RX) */
#define __ATM_LM_MKRMT(n)	((n) << 8)  /* Remote (i.e. loop RX to TX) */

#define __ATM_LM_XTLOC(n)	((n) & 0xff)
#define __ATM_LM_XTRMT(n)	(((n) >> 8) & 0xff)

#define ATM_LM_NONE	0	/* no loopback */

#define ATM_LM_LOC_AAL	__ATM_LM_MKLOC(__ATM_LM_AAL)
#define ATM_LM_LOC_ATM	__ATM_LM_MKLOC(__ATM_LM_ATM)
#define ATM_LM_LOC_PHY	__ATM_LM_MKLOC(__ATM_LM_PHY)
#define ATM_LM_LOC_ANALOG __ATM_LM_MKLOC(__ATM_LM_ANALOG)

#define ATM_LM_RMT_AAL	__ATM_LM_MKRMT(__ATM_LM_AAL)
#define ATM_LM_RMT_ATM	__ATM_LM_MKRMT(__ATM_LM_ATM)
#define ATM_LM_RMT_PHY	__ATM_LM_MKRMT(__ATM_LM_PHY)
#define ATM_LM_RMT_ANALOG __ATM_LM_MKRMT(__ATM_LM_ANALOG)

/*
 * Note: ATM_LM_LOC_* and ATM_LM_RMT_* can be combined, provided that
 * __ATM_LM_XTLOC(x) <= __ATM_LM_XTRMT(x)
 */


struct atm_iobuf {
	int length;
	void *buffer;
};

/* for ATM_GETCIRANGE / ATM_SETCIRANGE */

#define ATM_CI_MAX      -1              /* use maximum range of VPI/VCI */
 
struct atm_cirange {
	signed char	vpi_bits;	/* 1..8, ATM_CI_MAX (-1) for maximum */
	signed char	vci_bits;	/* 1..16, ATM_CI_MAX (-1) for maximum */
};

/* for ATM_SETSC; actually taken from the ATM_VF number space */

#define ATM_SC_RX	1024		/* enable RX single-copy */
#define ATM_SC_TX	2048		/* enable TX single-copy */

#define ATM_BACKLOG_DEFAULT 32 /* if we get more, we're likely to time out
				  anyway */

/* MF: change_qos (Modify) flags */

#define ATM_MF_IMMED	 1	/* Block until change is effective */
#define ATM_MF_INC_RSV	 2	/* Change reservation on increase */
#define ATM_MF_INC_SHP	 4	/* Change shaping on increase */
#define ATM_MF_DEC_RSV	 8	/* Change reservation on decrease */
#define ATM_MF_DEC_SHP	16	/* Change shaping on decrease */
#define ATM_MF_BWD	32	/* Set the backward direction parameters */

#define ATM_MF_SET	(ATM_MF_INC_RSV | ATM_MF_INC_SHP | ATM_MF_DEC_RSV | \
			  ATM_MF_DEC_SHP | ATM_MF_BWD)

/*
 * ATM_VS_* are used to express VC state in a human-friendly way.
 */

#define ATM_VS_IDLE	0	/* VC is not used */
#define ATM_VS_CONNECTED 1	/* VC is connected */
#define ATM_VS_CLOSING	2	/* VC is closing */
#define ATM_VS_LISTEN	3	/* VC is listening for incoming setups */
#define ATM_VS_INUSE	4	/* VC is in use (registered with atmsigd) */
#define ATM_VS_BOUND	5	/* VC is bound */

#define ATM_VS2TXT_MAP \
    "IDLE", "CONNECTED", "CLOSING", "LISTEN", "INUSE", "BOUND"

#define ATM_VF2TXT_MAP \
    "ADDR",	"READY",	"PARTIAL",	"REGIS", \
    "RELEASED", "HASQOS",	"LISTEN",	"META", \
    "256",	"512",		"1024",		"2048", \
    "SESSION",	"HASSAP",	"BOUND",	"CLOSE"



#endif /* LINUX_ATMDEV_H */
