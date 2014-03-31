/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************
 *
 *	dgap_conf.h - Header file for installations and parse files.
 *
 *	$Id: dgap_conf.h,v 1.1 2009/10/23 14:01:57 markh Exp $
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 */

#ifndef _DGAP_CONF_H
#define _DGAP_CONF_H

#define NULLNODE 0		/* header node, not used */
#define BNODE 1			/* Board node */
#define LNODE 2			/* Line node */
#define CNODE 3			/* Concentrator node */
#define MNODE 4			/* EBI Module node */
#define TNODE 5			/* tty name prefix node */
#define	CUNODE 6		/* cu name prefix (non-SCO) */
#define PNODE 7			/* trans. print prefix node */
#define JNODE 8			/* maJor number node */
#define ANODE 9			/* altpin */
#define	TSNODE 10		/* tty structure size */
#define CSNODE 11		/* channel structure size */
#define BSNODE 12		/* board structure size */
#define USNODE 13		/* unit schedule structure size */
#define FSNODE 14		/* f2200 structure size */
#define VSNODE 15		/* size of VPIX structures */
#define INTRNODE 16		/* enable interrupt */

/* Enumeration of tokens */
#define	BEGIN	1
#define	END	2
#define	BOARD	10

#define EPCFS	11 /* start of EPC family definitions */
#define	ICX		11
#define	MCX		13
#define PCX	14
#define	IEPC	15
#define	EEPC	16
#define	MEPC	17
#define	IPCM	18
#define	EPCM	19
#define	MPCM	20
#define PEPC	21
#define PPCM	22
#ifdef CP
#define ICP     23
#define ECP     24
#define MCP     25
#endif
#define EPCFE	25 /* end of EPC family definitions */
#define	PC2E	26
#define	PC4E	27
#define	PC4E8K	28
#define	PC8E	29
#define	PC8E8K	30
#define	PC16E	31
#define MC2E8K  34
#define MC4E8K  35
#define MC8E8K  36

#define AVANFS	42	/* start of Avanstar family definitions */
#define A8P 	42
#define A16P	43
#define AVANFE	43	/* end of Avanstar family definitions */

#define DA2000FS	44	/* start of AccelePort 2000 family definitions */
#define DA22 		44 /* AccelePort 2002 */
#define DA24 		45 /* AccelePort 2004 */
#define DA28		46 /* AccelePort 2008 */
#define DA216		47 /* AccelePort 2016 */
#define DAR4		48 /* AccelePort RAS 4 port */
#define DAR8		49 /* AccelePort RAS 8 port */
#define DDR24		50 /* DataFire RAS 24 port */
#define DDR30		51 /* DataFire RAS 30 port */
#define DDR48		52 /* DataFire RAS 48 port */
#define DDR60		53 /* DataFire RAS 60 port */
#define DA2000FE	53 /* end of AccelePort 2000/RAS family definitions */

#define PCXRFS	106	/* start of PCXR family definitions */
#define	APORT4	106
#define	APORT8	107
#define PAPORT4 108
#define PAPORT8 109
#define APORT4_920I	110
#define APORT8_920I	111
#define APORT4_920P	112
#define APORT8_920P	113
#define APORT2_920P 114
#define PCXRFE	117	/* end of PCXR family definitions */

#define	LINE	82
#ifdef T1
#define T1M	83
#define E1M	84
#endif
#define	CONC	64
#define	CX	65
#define	EPC	66
#define	MOD	67
#define	PORTS	68
#define METHOD	69
#define CUSTOM	70
#define BASIC	71
#define STATUS	72
#define MODEM	73
/* The following tokens can appear in multiple places */
#define	SPEED	74
#define	NPORTS	75
#define	ID	76
#define CABLE	77
#define CONNECT	78
#define	IO	79
#define	MEM	80
#define DPSZ	81

#define	TTYN	90
#define	CU	91
#define	PRINT	92
#define	XPRINT	93
#define CMAJOR   94
#define ALTPIN  95
#define STARTO 96
#define USEINTR  97
#define PCIINFO  98

#define	TTSIZ	100
#define	CHSIZ	101
#define BSSIZ	102
#define	UNTSIZ	103
#define	F2SIZ	104
#define	VPSIZ	105

#define	TOTAL_BOARD	2
#define	CURRENT_BRD	4
#define	BOARD_TYPE	6
#define	IO_ADDRESS	8
#define	MEM_ADDRESS	10

#define	FIELDS_PER_PAGE	18

#define TB_FIELD	1
#define CB_FIELD	3
#define BT_FIELD	5
#define IO_FIELD	7
#define ID_FIELD	8
#define ME_FIELD	9
#define TTY_FIELD	11
#define CU_FIELD	13
#define PR_FIELD	15
#define MPR_FIELD	17

#define	MAX_FIELD	512

#define	INIT		0
#define	NITEMS		128
#define MAX_ITEM	512

#define	DSCRINST	1
#define	DSCRNUM		3
#define	ALTPINQ		5
#define	SSAVE		7

#define	DSCR		"32"
#define	ONETONINE	"123456789"
#define	ALL		"1234567890"


struct cnode {
	struct cnode *next;
	int type;
	int numbrd;

	union {
		struct {
			char  type;	/* Board Type 		*/
			short port;	/* I/O Address		*/
			char  *portstr; /* I/O Address in string */
			long  addr;	/* Memory Address	*/
			char  *addrstr; /* Memory Address in string */
			long  pcibus;	/* PCI BUS		*/
			char  *pcibusstr; /* PCI BUS in string */
			long  pcislot;	/* PCI SLOT		*/
			char  *pcislotstr; /* PCI SLOT in string */
			char  nport;	/* Number of Ports	*/
			char  *id;	/* tty id		*/
			int   start;	/* start of tty counting */
			char  *method;  /* Install method       */
			char  v_type;
			char  v_port;
			char  v_addr;
			char  v_pcibus;
			char  v_pcislot;
			char  v_nport;
			char  v_id;
			char  v_start;
			char  v_method;
			char  line1;
			char  line2;
			char  conc1;   /* total concs in line1 */
			char  conc2;   /* total concs in line2 */
			char  module1; /* total modules for line1 */
			char  module2; /* total modules for line2 */
			char  *status; /* config status */
			char  *dimstatus;	 /* Y/N */
			int   status_index; /* field pointer */
		} board;

		struct {
			char  *cable;
			char  v_cable;
			char  speed;
			char  v_speed;
		} line;

		struct {
			char  type;
			char  *connect;
			char  speed;
			char  nport;
			char  *id;
			char  *idstr;
			int   start;
			char  v_type;
			char  v_connect;
			char  v_speed;
			char  v_nport;
			char  v_id;
			char  v_start;
		} conc;

		struct {
			char type;
			char nport;
			char *id;
			char *idstr;
			int  start;
			char v_type;
			char v_nport;
			char v_id;
			char v_start;
		} module;

		char *ttyname;

		char *cuname;

		char *printname;

		int  majornumber;

		int  altpin;

		int  ttysize;

		int  chsize;

		int  bssize;

		int  unsize;

		int  f2size;

		int  vpixsize;

		int  useintr;
	} u;
};

#endif
