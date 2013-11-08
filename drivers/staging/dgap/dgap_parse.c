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
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE! 
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com. 
 *	Thank you.
 *
 *
 *****************************************************************************
 *
 * dgap_parse.c - Parses the configuration information from the input file.
 *
 * $Id: dgap_parse.c,v 1.1 2009/10/23 14:01:57 markh Exp $
 *
 */
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/slab.h>

#include "dgap_types.h"
#include "dgap_fep5.h"
#include "dgap_driver.h"
#include "dgap_conf.h"


/*
 * Function prototypes.
 */
static int dgap_gettok(char **in, struct cnode *p);
static char *dgap_getword(char **in);
static char *dgap_savestring(char *s);
static struct cnode *dgap_newnode(int t);
static int dgap_checknode(struct cnode *p);
static void dgap_err(char *s);

/*
 * Our needed internal static variables...
 */
static struct cnode dgap_head;
#define MAXCWORD 200
static char dgap_cword[MAXCWORD];

struct toklist {
	int	token;
	char	*string;
};

static struct toklist dgap_tlist[] = {
	{	BEGIN,		"config_begin"			},
	{	END,		"config_end"			},
	{	BOARD,		"board"				},
	{	PCX,		"Digi_AccelePort_C/X_PCI"	},	/* C/X_PCI */
	{	PEPC,		"Digi_AccelePort_EPC/X_PCI"	},	/* EPC/X_PCI */
	{	PPCM,		"Digi_AccelePort_Xem_PCI"	},	/* PCI/Xem */
	{	APORT2_920P,	"Digi_AccelePort_2r_920_PCI"	},
	{	APORT4_920P,	"Digi_AccelePort_4r_920_PCI"	},
	{	APORT8_920P,	"Digi_AccelePort_8r_920_PCI"	},
	{	PAPORT4,	"Digi_AccelePort_4r_PCI(EIA-232/RS-422)" },
	{	PAPORT8,	"Digi_AccelePort_8r_PCI(EIA-232/RS-422)" },
	{	IO,		"io"				},
	{	PCIINFO,	"pciinfo"			},
	{	LINE,		"line"				},
	{	CONC,		"conc"				},
	{	CONC,		"concentrator"			},
	{	CX,		"cx"				},
	{	CX,		"ccon"				},
	{	EPC,		"epccon"			},
	{	EPC,		"epc"				},
	{	MOD,		"module"			},
	{	ID,		"id"				},
	{	STARTO,		"start"				},
	{	SPEED,		"speed"				},
	{	CABLE,		"cable"				},
	{	CONNECT,	"connect"			},
	{	METHOD,		"method"			},
	{	STATUS,		"status"			},
	{	CUSTOM,		"Custom"			},
	{	BASIC,		"Basic"				},
	{	MEM,		"mem"				},
	{	MEM,		"memory"			},
	{	PORTS,		"ports"				},
	{	MODEM,		"modem"				},
	{	NPORTS,		"nports"			},
	{	TTYN,		"ttyname"			},
	{	CU,		"cuname"			},
	{	PRINT,		"prname"			},
	{	CMAJOR,		"major"				},
	{	ALTPIN,		"altpin"			},
	{	USEINTR,	"useintr"			},
	{	TTSIZ,		"ttysize"			},
	{	CHSIZ,		"chsize"			},
	{	BSSIZ,		"boardsize"			},
	{	UNTSIZ,		"schedsize"			},
	{	F2SIZ,		"f2200size"			},
	{	VPSIZ,		"vpixsize"			},
	{	0,		NULL				}
};


/*
 * Parse a configuration file read into memory as a string.
 */
int	dgap_parsefile(char **in, int Remove)
{
	struct cnode *p, *brd, *line, *conc;
	int	rc;
	char	*s = NULL, *s2 = NULL;
	int	linecnt = 0;

	p = &dgap_head;
	brd = line = conc = NULL;

	/* perhaps we are adding to an existing list? */
	while (p->next != NULL) {
		p = p->next;
	}

	/* file must start with a BEGIN */
	while ( (rc = dgap_gettok(in,p)) != BEGIN ) {
		if (rc == 0) {
			dgap_err("unexpected EOF");
			return(-1);
		}
	}

	for (; ; ) {
		rc = dgap_gettok(in,p);
		if (rc == 0) {
			dgap_err("unexpected EOF");
			return(-1);
		}

		switch (rc) {
		case 0:
			dgap_err("unexpected end of file");
			return(-1);

		case BEGIN:	/* should only be 1 begin */
			dgap_err("unexpected config_begin\n");
			return(-1);

		case END:
			return(0);

		case BOARD:	/* board info */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(BNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;

			p->u.board.status = dgap_savestring("No");
			line = conc = NULL;
			brd = p;
			linecnt = -1;
			break;

		case APORT2_920P:	/* AccelePort_4 */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_2r_920 string");
				return(-1);
			}
			p->u.board.type = APORT2_920P;
			p->u.board.v_type = 1;
			DPR_INIT(("Adding Digi_2r_920 PCI to config...\n"));
			break;

		case APORT4_920P:	/* AccelePort_4 */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_4r_920 string");
				return(-1);
			}
			p->u.board.type = APORT4_920P;
			p->u.board.v_type = 1;
			DPR_INIT(("Adding Digi_4r_920 PCI to config...\n"));
			break;

		case APORT8_920P:	/* AccelePort_8 */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_8r_920 string");
				return(-1);
			}
			p->u.board.type = APORT8_920P;
			p->u.board.v_type = 1;
			DPR_INIT(("Adding Digi_8r_920 PCI to config...\n"));
			break;

		case PAPORT4:	/* AccelePort_4 PCI */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_4r(PCI) string");
				return(-1);
			}
			p->u.board.type = PAPORT4;
			p->u.board.v_type = 1;
			DPR_INIT(("Adding Digi_4r PCI to config...\n"));
			break;

		case PAPORT8:	/* AccelePort_8 PCI */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_8r string");
				return(-1);
			}
			p->u.board.type = PAPORT8;
			p->u.board.v_type = 1;
			DPR_INIT(("Adding Digi_8r PCI to config...\n"));
			break;

		case PCX:	/* PCI C/X */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_C/X_(PCI) string");
				return(-1);
			}
			p->u.board.type = PCX;
			p->u.board.v_type = 1;
			p->u.board.conc1 = 0;
			p->u.board.conc2 = 0;
			p->u.board.module1 = 0;
			p->u.board.module2 = 0;
			DPR_INIT(("Adding PCI C/X to config...\n"));
			break;

		case PEPC:	/* PCI EPC/X */
			if (p->type != BNODE) {
				dgap_err("unexpected \"Digi_EPC/X_(PCI)\" string");
				return(-1);
			}
			p->u.board.type = PEPC;
			p->u.board.v_type = 1;
			p->u.board.conc1 = 0;
			p->u.board.conc2 = 0;
			p->u.board.module1 = 0;
			p->u.board.module2 = 0;
			DPR_INIT(("Adding PCI EPC/X to config...\n"));
			break;

		case PPCM:	/* PCI/Xem */
			if (p->type != BNODE) {
				dgap_err("unexpected PCI/Xem string");
				return(-1);
			}
			p->u.board.type = PPCM;
			p->u.board.v_type = 1;
			p->u.board.conc1 = 0;
			p->u.board.conc2 = 0;
			DPR_INIT(("Adding PCI XEM to config...\n"));
			break;

		case IO:	/* i/o port */
			if (p->type != BNODE) {
				dgap_err("IO port only vaild for boards");
				return(-1);
			}
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.board.portstr = dgap_savestring(s);
			p->u.board.port = (short)simple_strtol(s, &s2, 0);
			if ((short)strlen(s) > (short)(s2 - s)) {
				dgap_err("bad number for IO port");
				return(-1);
			}
			p->u.board.v_port = 1;
			DPR_INIT(("Adding IO (%s) to config...\n", s));
			break;

		case MEM:	/* memory address */
			if (p->type != BNODE) {
				dgap_err("memory address only vaild for boards");
				return(-1);
			}
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.board.addrstr = dgap_savestring(s);
			p->u.board.addr = simple_strtoul(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for memory address");
				return(-1);
			}
			p->u.board.v_addr = 1;
			DPR_INIT(("Adding MEM (%s) to config...\n", s));
			break;

		case PCIINFO:	/* pci information */
			if (p->type != BNODE) {
				dgap_err("memory address only vaild for boards");
				return(-1);
			}
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.board.pcibusstr = dgap_savestring(s);
			p->u.board.pcibus = simple_strtoul(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for pci bus");
				return(-1);
			}
			p->u.board.v_pcibus = 1;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.board.pcislotstr = dgap_savestring(s);
			p->u.board.pcislot = simple_strtoul(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for pci slot");
				return(-1);
			}
			p->u.board.v_pcislot = 1;

			DPR_INIT(("Adding PCIINFO (%s %s) to config...\n", p->u.board.pcibusstr, 
				p->u.board.pcislotstr));
			break;

		case METHOD:
			if (p->type != BNODE) {
				dgap_err("install method only vaild for boards");
				return(-1);
			}
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.board.method = dgap_savestring(s);
			p->u.board.v_method = 1;
			DPR_INIT(("Adding METHOD (%s) to config...\n", s));
			break;

		case STATUS:
			if (p->type != BNODE) {
				dgap_err("config status only vaild for boards");
				return(-1);
			}
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.board.status = dgap_savestring(s);
			DPR_INIT(("Adding STATUS (%s) to config...\n", s));
			break;

		case NPORTS:	/* number of ports */
			if (p->type == BNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.board.nport = (char)simple_strtol(s, &s2, 0);
				if ((int)strlen(s) > (int)(s2 - s)) {
					dgap_err("bad number for number of ports");
					return(-1);
				}
				p->u.board.v_nport = 1;
			} else if (p->type == CNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.conc.nport = (char)simple_strtol(s, &s2, 0);
				if ((int)strlen(s) > (int)(s2 - s)) {
					dgap_err("bad number for number of ports");
					return(-1);
				}
				p->u.conc.v_nport = 1;
			} else if (p->type == MNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.module.nport = (char)simple_strtol(s, &s2, 0);
				if ((int)strlen(s) > (int)(s2 - s)) {
					dgap_err("bad number for number of ports");
					return(-1);
				}
				p->u.module.v_nport = 1;
			} else {
				dgap_err("nports only valid for concentrators or modules");
				return(-1);
			}
			DPR_INIT(("Adding NPORTS (%s) to config...\n", s));
			break;

		case ID:	/* letter ID used in tty name */
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}

			p->u.board.status = dgap_savestring(s);

			if (p->type == CNODE) {
				p->u.conc.id = dgap_savestring(s);
				p->u.conc.v_id = 1;
			} else if (p->type == MNODE) {
				p->u.module.id = dgap_savestring(s);
				p->u.module.v_id = 1;
			} else {
				dgap_err("id only valid for concentrators or modules");
				return(-1);
			}
			DPR_INIT(("Adding ID (%s) to config...\n", s));
			break;

		case STARTO:	/* start offset of ID */
			if (p->type == BNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.board.start = simple_strtol(s, &s2, 0);
				if ((int)strlen(s) > (int)(s2 - s)) {
					dgap_err("bad number for start of tty count");
					return(-1);
				}
				p->u.board.v_start = 1;
			} else if (p->type == CNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.conc.start = simple_strtol(s, &s2, 0);
				if ((int)strlen(s) > (int)(s2 - s)) {
					dgap_err("bad number for start of tty count");
					return(-1);
				}
				p->u.conc.v_start = 1;
			} else if (p->type == MNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.module.start = simple_strtol(s, &s2, 0);
				if ((int)strlen(s) > (int)(s2 - s)) {
					dgap_err("bad number for start of tty count");
					return(-1);
				}
				p->u.module.v_start = 1;
			} else {
				dgap_err("start only valid for concentrators or modules");
				return(-1);
			}
			DPR_INIT(("Adding START (%s) to config...\n", s));
			break;

		case TTYN:	/* tty name prefix */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(TNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			if ( (s = dgap_getword(in)) == NULL ) {
				dgap_err("unexpeced end of file");
				return(-1);
			}
			if ( (p->u.ttyname = dgap_savestring(s)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			DPR_INIT(("Adding TTY (%s) to config...\n", s));
			break;

		case CU:	/* cu name prefix */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(CUNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			if ( (s = dgap_getword(in)) == NULL ) {
				dgap_err("unexpeced end of file");
				return(-1);
			}
			if ( (p->u.cuname = dgap_savestring(s)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			DPR_INIT(("Adding CU (%s) to config...\n", s));
			break;

		case LINE:	/* line information */
			if (dgap_checknode(p))
				return(-1);
			if (brd == NULL) {
				dgap_err("must specify board before line info");
				return(-1);
			}
			switch (brd->u.board.type) {
			case PPCM:
				dgap_err("line not vaild for PC/em");
				return(-1);
			}
			if ( (p->next = dgap_newnode(LNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			conc = NULL;
			line = p;
			linecnt++;
			DPR_INIT(("Adding LINE to config...\n"));
			break;

		case CONC:	/* concentrator information */
			if (dgap_checknode(p))
				return(-1);
			if (line == NULL) {
				dgap_err("must specify line info before concentrator");
				return(-1);
			}
			if ( (p->next = dgap_newnode(CNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			conc = p;
			if (linecnt)
				brd->u.board.conc2++;
			else
				brd->u.board.conc1++;

			DPR_INIT(("Adding CONC to config...\n"));
			break;

		case CX:	/* c/x type concentrator */
			if (p->type != CNODE) {
				dgap_err("cx only valid for concentrators");
				return(-1);
			}
			p->u.conc.type = CX;
			p->u.conc.v_type = 1;
			DPR_INIT(("Adding CX to config...\n"));
			break;

		case EPC:	/* epc type concentrator */
			if (p->type != CNODE) {
				dgap_err("cx only valid for concentrators");
				return(-1);
			}
			p->u.conc.type = EPC;
			p->u.conc.v_type = 1;
			DPR_INIT(("Adding EPC to config...\n"));
			break;

		case MOD:	/* EBI module */
			if (dgap_checknode(p))
				return(-1);
			if (brd == NULL) {
				dgap_err("must specify board info before EBI modules");
				return(-1);
			}
			switch (brd->u.board.type) {
			case PPCM:
				linecnt = 0;
				break;
			default:
				if (conc == NULL) {
					dgap_err("must specify concentrator info before EBI module");
					return(-1);
				}
			}
			if ( (p->next = dgap_newnode(MNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			if (linecnt)
				brd->u.board.module2++;
			else
				brd->u.board.module1++;

			DPR_INIT(("Adding MOD to config...\n"));
			break;

		case PORTS:	/* ports type EBI module */
			if (p->type != MNODE) {
				dgap_err("ports only valid for EBI modules");
				return(-1);
			}
			p->u.module.type = PORTS;
			p->u.module.v_type = 1;
			DPR_INIT(("Adding PORTS to config...\n"));
			break;

		case MODEM:	/* ports type EBI module */
			if (p->type != MNODE) {
				dgap_err("modem only valid for modem modules");
				return(-1);
			}
			p->u.module.type = MODEM;
			p->u.module.v_type = 1;
			DPR_INIT(("Adding MODEM to config...\n"));
			break;

		case CABLE:
			if (p->type == LNODE) {
				if ((s = dgap_getword(in)) == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.line.cable = dgap_savestring(s);
				p->u.line.v_cable = 1;
			}
			DPR_INIT(("Adding CABLE (%s) to config...\n", s));
			break;

		case SPEED:	/* sync line speed indication */
			if (p->type == LNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.line.speed = (char)simple_strtol(s, &s2, 0);
				if ((short)strlen(s) > (short)(s2 - s)) {
					dgap_err("bad number for line speed");
					return(-1);
				}
				p->u.line.v_speed = 1;
			} else if (p->type == CNODE) {
				s = dgap_getword(in);
				if (s == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.conc.speed = (char)simple_strtol(s, &s2, 0);
				if ((short)strlen(s) > (short)(s2 - s)) {
					dgap_err("bad number for line speed");
					return(-1);
				}
				p->u.conc.v_speed = 1;
			} else {
				dgap_err("speed valid only for lines or concentrators.");
				return(-1);
			}
			DPR_INIT(("Adding SPEED (%s) to config...\n", s));
			break;

		case CONNECT:
			if (p->type == CNODE) {
				if ((s = dgap_getword(in)) == NULL) {
					dgap_err("unexpected end of file");
					return(-1);
				}
				p->u.conc.connect = dgap_savestring(s);
				p->u.conc.v_connect = 1;
			}
			DPR_INIT(("Adding CONNECT (%s) to config...\n", s));
			break;
		case PRINT:	/* transparent print name prefix */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(PNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			if ( (s = dgap_getword(in)) == NULL ) {
				dgap_err("unexpeced end of file");
				return(-1);
			}
			if ( (p->u.printname = dgap_savestring(s)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			DPR_INIT(("Adding PRINT (%s) to config...\n", s));
			break;

		case CMAJOR:	/* major number */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(JNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.majornumber = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for major number");
				return(-1);
			}
			DPR_INIT(("Adding CMAJOR (%s) to config...\n", s));
			break;

		case ALTPIN:	/* altpin setting */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(ANODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.altpin = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for altpin");
				return(-1);
			}
			DPR_INIT(("Adding ALTPIN (%s) to config...\n", s));
			break;

		case USEINTR:		/* enable interrupt setting */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(INTRNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.useintr = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for useintr");
				return(-1);
			}
			DPR_INIT(("Adding USEINTR (%s) to config...\n", s));
			break;

		case TTSIZ:	/* size of tty structure */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(TSNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.ttysize = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for ttysize");
				return(-1);
			}
			DPR_INIT(("Adding TTSIZ (%s) to config...\n", s));
			break;

		case CHSIZ:	/* channel structure size */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(CSNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.chsize = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for chsize");
				return(-1);
			}
			DPR_INIT(("Adding CHSIZE (%s) to config...\n", s));
			break;

		case BSSIZ:	/* board structure size */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(BSNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.bssize = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for bssize");
				return(-1);
			}
			DPR_INIT(("Adding BSSIZ (%s) to config...\n", s));
			break;

		case UNTSIZ:	/* sched structure size */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(USNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.unsize = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for schedsize");
				return(-1);
			}
			DPR_INIT(("Adding UNTSIZ (%s) to config...\n", s));
			break;

		case F2SIZ:	/* f2200 structure size */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(FSNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.f2size = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for f2200size");
				return(-1);
			}
			DPR_INIT(("Adding F2SIZ (%s) to config...\n", s));
			break;

		case VPSIZ:	/* vpix structure size */
			if (dgap_checknode(p))
				return(-1);
			if ( (p->next = dgap_newnode(VSNODE)) == NULL ) {
				dgap_err("out of memory");
				return(-1);
			}
			p = p->next;
			s = dgap_getword(in);
			if (s == NULL) {
				dgap_err("unexpected end of file");
				return(-1);
			}
			p->u.vpixsize = simple_strtol(s, &s2, 0);
			if ((int)strlen(s) > (int)(s2 - s)) {
				dgap_err("bad number for vpixsize");
				return(-1);
			}
			DPR_INIT(("Adding VPSIZ (%s) to config...\n", s));
			break;
		}
	}
}


/*
 * dgap_sindex: much like index(), but it looks for a match of any character in
 * the group, and returns that position.  If the first character is a ^, then
 * this will match the first occurence not in that group.
 */
static char *dgap_sindex (char *string, char *group)
{
	char    *ptr;

	if (!string || !group)
		return (char *) NULL;

	if (*group == '^') {   
		group++;
		for (; *string; string++) {
			for (ptr = group; *ptr; ptr++) {
				if (*ptr == *string)
					break;
			}
			if (*ptr == '\0')
				return string;
		}
	}   
	else {
		for (; *string; string++) {
			for (ptr = group; *ptr; ptr++) {
				if (*ptr == *string)
					return string;
			}
		}
	}

	return (char *) NULL;
}


/*
 * Get a token from the input file; return 0 if end of file is reached
 */
static int dgap_gettok(char **in, struct cnode *p)
{
	char	*w;
	struct toklist *t;
	
	if (strstr(dgap_cword, "boar")) {
		w = dgap_getword(in);
		snprintf(dgap_cword, MAXCWORD, "%s", w);
		for (t = dgap_tlist; t->token != 0; t++) {
			if ( !strcmp(w, t->string)) {
				return(t->token);
			} 
		}
		dgap_err("board !!type not specified");
		return(1);
	}
	else {
		while ( (w = dgap_getword(in)) != NULL ) {
			snprintf(dgap_cword, MAXCWORD, "%s", w);
			for (t = dgap_tlist; t->token != 0; t++) {
				if ( !strcmp(w, t->string) )
					return(t->token);
			}
		}
		return(0);
	}
}


/*
 * get a word from the input stream, also keep track of current line number.
 * words are separated by whitespace.
 */
static char *dgap_getword(char **in)
{
	char *ret_ptr = *in;

        char *ptr = dgap_sindex(*in, " \t\n");

	/* If no word found, return null */
	if (!ptr)
		return NULL;

	/* Mark new location for our buffer */
	*ptr = '\0';
	*in = ptr + 1;

	/* Eat any extra spaces/tabs/newlines that might be present */
	while (*in && **in && ((**in == ' ') || (**in == '\t') || (**in == '\n'))) {
		**in = '\0';
		*in = *in + 1;
	}

	return ret_ptr;
}


/*
 * print an error message, giving the line number in the file where
 * the error occurred.
 */
static void dgap_err(char *s)
{
	printk("DGAP: parse: %s\n", s);
}


/*
 * allocate a new configuration node of type t
 */
static struct cnode *dgap_newnode(int t)
{
	struct cnode *n;
	if ( (n = (struct cnode *) kmalloc(sizeof(struct cnode ), GFP_ATOMIC) ) != NULL) {
		memset( (char *)n, 0, sizeof(struct cnode ) );
		n->type = t;
	}
	return(n);
}


/*
 * dgap_checknode: see if all the necessary info has been supplied for a node
 * before creating the next node.
 */
static int dgap_checknode(struct cnode *p)
{
	switch (p->type) {
	case BNODE:
		if (p->u.board.v_type == 0) {
			dgap_err("board type !not specified");
			return(1);
		}

		return(0);

	case LNODE:
		if (p->u.line.v_speed == 0) {
			dgap_err("line speed not specified");
			return(1);
		}
		return(0);

	case CNODE:
		if (p->u.conc.v_type == 0) {
			dgap_err("concentrator type not specified");
			return(1);
		}
		if (p->u.conc.v_speed == 0) {
			dgap_err("concentrator line speed not specified");
			return(1);
		}
		if (p->u.conc.v_nport == 0) {
			dgap_err("number of ports on concentrator not specified");
			return(1);
		}
		if (p->u.conc.v_id == 0) {
			dgap_err("concentrator id letter not specified");
			return(1);
		}
		return(0);

	case MNODE:
		if (p->u.module.v_type == 0) {
			dgap_err("EBI module type not specified");
			return(1);
		}
		if (p->u.module.v_nport == 0) {
			dgap_err("number of ports on EBI module not specified");
			return(1);
		}
		if (p->u.module.v_id == 0) {
			dgap_err("EBI module id letter not specified");
			return(1);
		}
		return(0);
	}
	return(0);
}

/*
 * save a string somewhere
 */
static char	*dgap_savestring(char *s)
{
	char	*p;
	if ( (p = kmalloc(strlen(s) + 1, GFP_ATOMIC) ) != NULL) {
		strcpy(p, s);
	}
	return(p);
}


/*
 * Given a board pointer, returns whether we should use interrupts or not.
 */
uint dgap_config_get_useintr(struct board_t *bd)
{
	struct cnode *p = NULL;

	if (!bd)
		return(0);

	for (p = bd->bd_config; p; p = p->next) {
		switch (p->type) {
		case INTRNODE:
			/*
			 * check for pcxr types.
			 */
			return p->u.useintr;
		default:
			break;
		}
	}

	/* If not found, then don't turn on interrupts. */
	return 0;
}


/*
 * Given a board pointer, returns whether we turn on altpin or not.
 */
uint dgap_config_get_altpin(struct board_t *bd)
{
	struct cnode *p = NULL;

	if (!bd)
		return(0);

	for (p = bd->bd_config; p; p = p->next) {
		switch (p->type) {
		case ANODE:
			/*
			 * check for pcxr types.
			 */
			return p->u.altpin;
		default:
			break;
		}
	}

	/* If not found, then don't turn on interrupts. */
	return 0;
}



/*
 * Given a specific type of board, if found, detached link and 
 * returns the first occurance in the list.
 */
struct cnode *dgap_find_config(int type, int bus, int slot)
{
	struct cnode *p, *prev = NULL, *prev2 = NULL, *found = NULL;

	p = &dgap_head;

	while (p->next != NULL) {
		prev = p;
		p = p->next;

		if (p->type == BNODE) {

			if (p->u.board.type == type) {

				if (p->u.board.v_pcibus && p->u.board.pcibus != bus) {
					DPR(("Found matching board, but wrong bus position. System says bus %d, we want bus %ld\n",
						bus, p->u.board.pcibus));
					continue;
				}
				if (p->u.board.v_pcislot && p->u.board.pcislot != slot) {
					DPR_INIT(("Found matching board, but wrong slot position. System says slot %d, we want slot %ld\n",
						slot, p->u.board.pcislot));
					continue;
				}

				DPR_INIT(("Matched type in config file\n"));

				found = p;
				/*
				 * Keep walking thru the list till we find the next board.
				 */
				while (p->next != NULL) {
					prev2 = p;
					p = p->next;
					if (p->type == BNODE) {

						/*
						 * Mark the end of our 1 board chain of configs.
						 */
						prev2->next = NULL;

						/*
						 * Link the "next" board to the previous board,
						 * effectively "unlinking" our board from the main config.
						 */
						prev->next = p;

						return found;
					}
				}
				/*
				 * It must be the last board in the list.
				 */
				prev->next = NULL;
				return found;
			}
		}
	}
	return NULL;
}

/*
 * Given a board pointer, walks the config link, counting up
 * all ports user specified should be on the board.
 * (This does NOT mean they are all actually present right now tho)
 */
uint dgap_config_get_number_of_ports(struct board_t *bd)
{
	int count = 0;
	struct cnode *p = NULL;

	if (!bd)
		return(0);

	for (p = bd->bd_config; p; p = p->next) {

		switch (p->type) {
		case BNODE:
			/*
			 * check for pcxr types.
			 */
			if (p->u.board.type > EPCFE)
				count += p->u.board.nport;
			break;
		case CNODE:
			count += p->u.conc.nport;
			break;
		case MNODE:
			count += p->u.module.nport;
			break;
		}
	}
	return (count);
}

char *dgap_create_config_string(struct board_t *bd, char *string)
{
	char *ptr = string;
	struct cnode *p = NULL;
	struct cnode *q = NULL;
	int speed;

	if (!bd) {
		*ptr = 0xff;
		return string;
	}

	for (p = bd->bd_config; p; p = p->next) {

		switch (p->type) {
		case LNODE:
			*ptr = '\0';
			ptr++;
			*ptr = p->u.line.speed;
			ptr++;
			break;
		case CNODE:
			/*
			 * Because the EPC/con concentrators can have EM modules
			 * hanging off of them, we have to walk ahead in the list
			 * and keep adding the number of ports on each EM to the config.
			 * UGH!
			 */
			speed = p->u.conc.speed;
			q = p->next;
			if ((q != NULL) && (q->type == MNODE) ) {
				*ptr = (p->u.conc.nport + 0x80);
				ptr++;
				p = q;
				while ((q->next != NULL) && (q->next->type) == MNODE) {
					*ptr = (q->u.module.nport + 0x80);
					ptr++;
					p = q;
					q = q->next;
				}
				*ptr = q->u.module.nport;
				ptr++;
			} else {
				*ptr = p->u.conc.nport;
				ptr++;
			}

			*ptr = speed;
			ptr++;
			break;
		}
	}

	*ptr = 0xff;
	return string;
}



char *dgap_get_config_letters(struct board_t *bd, char *string)
{
	int found = FALSE;
	char *ptr = string;
	struct cnode *cptr = NULL;
	int len = 0;
	int left = MAXTTYNAMELEN;

	if (!bd) {
		return "<NULL>";
	}

	for (cptr = bd->bd_config; cptr; cptr = cptr->next) {

		if ((cptr->type == BNODE) &&
		     ((cptr->u.board.type == APORT2_920P) || (cptr->u.board.type == APORT4_920P) ||
		     (cptr->u.board.type == APORT8_920P) || (cptr->u.board.type == PAPORT4) ||
		     (cptr->u.board.type == PAPORT8))) {

			found = TRUE;
		}

		if (cptr->type == TNODE && found == TRUE) {
			char *ptr1;
			if (strstr(cptr->u.ttyname, "tty")) {
				ptr1 = cptr->u.ttyname;
				ptr1 += 3;
			}
			else {
				ptr1 = cptr->u.ttyname;
			}
			if (ptr1) {
				len = snprintf(ptr, left, "%s", ptr1);
				left -= len;
				ptr  += len;
				if (left <= 0)
					break;
			}
		}

		if (cptr->type == CNODE) {
			if (cptr->u.conc.id) {
				len = snprintf(ptr, left, "%s", cptr->u.conc.id);
				left -= len;
				ptr  += len;
				if (left <= 0)
					break;
			}
                }

		if (cptr->type == MNODE) {
			if (cptr->u.module.id) {
				len = snprintf(ptr, left, "%s", cptr->u.module.id);
				left -= len;
				ptr  += len;
				if (left <= 0)
					break;
			}
		}
	}

	return string;
}
