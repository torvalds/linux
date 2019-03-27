/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */
#include "ipf.h"


struct	ipopt_names	ionames[] ={
	{ IPOPT_NOP,	0x000001,	1,	"nop" },	/* RFC791 */
	{ IPOPT_RR,	0x000002,	8,	"rr" },		/* 1 route */
	{ IPOPT_ZSU,	0x000004,	4,	"zsu" },	/* size ?? */
	{ IPOPT_MTUP,	0x000008,	4,	"mtup" },	/* RFC1191 */
	{ IPOPT_MTUR,	0x000010,	4,	"mtur" },	/* RFC1191 */
	{ IPOPT_ENCODE,	0x000020,	4,	"encode" },	/* size ?? */
	{ IPOPT_TS,	0x000040,	8,	"ts" },		/* 1 TS */
	{ IPOPT_TR,	0x000080,	4,	"tr" },		/* RFC1393 */
	{ IPOPT_SECURITY,0x000100,	12,	"sec" },	/* RFC1108 */
	{ IPOPT_SECURITY,0x000100,	12,	"sec-class" },	/* RFC1108 */
	{ IPOPT_LSRR,	0x000200,	8,	"lsrr" },	/* 1 route */
	{ IPOPT_E_SEC,	0x000400,	8,	"e-sec" },	/* RFC1108 */
	{ IPOPT_CIPSO,	0x000800,	8,	"cipso" },	/* size ?? */
	{ IPOPT_SATID,	0x001000,	4,	"satid" },	/* RFC791 */
	{ IPOPT_SSRR,	0x002000,	8,	"ssrr" },	/* 1 route */
	{ IPOPT_ADDEXT,	0x004000,	4,	"addext" },	/* IPv7 ?? */
	{ IPOPT_VISA,	0x008000,	4,	"visa" },	/* size ?? */
	{ IPOPT_IMITD,	0x010000,	4,	"imitd" },	/* size ?? */
	{ IPOPT_EIP,	0x020000,	4,	"eip" },	/* RFC1385 */
	{ IPOPT_FINN,	0x040000,	4,	"finn" },	/* size ?? */
	{ IPOPT_DPS,	0x080000,	4,	"dps" },	/* size ?? */
	{ IPOPT_SDB,	0x100000,	4,	"sdb" },	/* size ?? */
	{ IPOPT_NSAPA,	0x200000,	4,	"nsapa" },	/* size ?? */
	{ IPOPT_RTRALRT,0x400000,	4,	"rtralrt" },	/* RFC2113 */
	{ IPOPT_UMP,	0x800000,	4,	"ump" },	/* size ?? */
	{ IPOPT_AH,	0x1000000,	0,	"ah" },		/* IPPROTO_AH */
	{ 0, 		0,	0,	(char *)NULL }     /* must be last */
};
