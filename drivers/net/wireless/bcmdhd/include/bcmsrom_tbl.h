/*
 * Table that encodes the srom formats for PCI/PCIe NICs.
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmsrom_tbl.h 427005 2013-10-02 00:15:10Z $
 */

#ifndef	_bcmsrom_tbl_h_
#define	_bcmsrom_tbl_h_

#include "sbpcmcia.h"
#include "wlioctl.h"

typedef struct {
	const char *name;
	uint32	revmask;
	uint32	flags;
	uint16	off;
	uint16	mask;
} sromvar_t;

#define SRFL_MORE	1		/* value continues as described by the next entry */
#define	SRFL_NOFFS	2		/* value bits can't be all one's */
#define	SRFL_PRHEX	4		/* value is in hexdecimal format */
#define	SRFL_PRSIGN	8		/* value is in signed decimal format */
#define	SRFL_CCODE	0x10		/* value is in country code format */
#define	SRFL_ETHADDR	0x20		/* value is an Ethernet address */
#define SRFL_LEDDC	0x40		/* value is an LED duty cycle */
#define SRFL_NOVAR	0x80		/* do not generate a nvram param, entry is for mfgc */
#define SRFL_ARRAY	0x100		/* value is in an array. All elements EXCEPT FOR THE LAST
					 * ONE in the array should have this flag set.
					 */


#define SROM_DEVID_PCIE	48

/* Assumptions:
 * - Ethernet address spans across 3 consective words
 *
 * Table rules:
 * - Add multiple entries next to each other if a value spans across multiple words
 *   (even multiple fields in the same word) with each entry except the last having
 *   it's SRFL_MORE bit set.
 * - Ethernet address entry does not follow above rule and must not have SRFL_MORE
 *   bit set. Its SRFL_ETHADDR bit implies it takes multiple words.
 * - The last entry's name field must be NULL to indicate the end of the table. Other
 *   entries must have non-NULL name.
 */

static const sromvar_t pci_sromvars[] = {
#if defined(CABLECPE)
	{"devid",	0xffffff00,	SRFL_PRHEX,	PCI_F0DEVID,	0xffff},
#elif defined(BCMPCIEDEV) && defined(BCMPCIEDEV_ENABLED)
	{"devid",	0xffffff00,	SRFL_PRHEX, SROM_DEVID_PCIE, 0xffff},
#else
	{"devid",	0xffffff00,	SRFL_PRHEX|SRFL_NOVAR,	PCI_F0DEVID,	0xffff},
#endif 
	{"boardrev",	0x0000000e,	SRFL_PRHEX,	SROM_AABREV,		SROM_BR_MASK},
	{"boardrev",	0x000000f0,	SRFL_PRHEX,	SROM4_BREV,		0xffff},
	{"boardrev",	0xffffff00,	SRFL_PRHEX,	SROM8_BREV,		0xffff},
	{"boardflags",	0x00000002,	SRFL_PRHEX,	SROM_BFL,		0xffff},
	{"boardflags",	0x00000004,	SRFL_PRHEX|SRFL_MORE,	SROM_BFL,	0xffff},
	{"",		0,		0,		SROM_BFL2,		0xffff},
	{"boardflags",	0x00000008,	SRFL_PRHEX|SRFL_MORE,	SROM_BFL,	0xffff},
	{"",		0,		0,		SROM3_BFL2,		0xffff},
	{"boardflags",	0x00000010,	SRFL_PRHEX|SRFL_MORE,	SROM4_BFL0,	0xffff},
	{"",		0,		0,		SROM4_BFL1,		0xffff},
	{"boardflags",	0x000000e0,	SRFL_PRHEX|SRFL_MORE,	SROM5_BFL0,	0xffff},
	{"",		0,		0,		SROM5_BFL1,		0xffff},
	{"boardflags",	0xffffff00,	SRFL_PRHEX|SRFL_MORE,	SROM8_BFL0,	0xffff},
	{"",		0,		0,		SROM8_BFL1,		0xffff},
	{"boardflags2", 0x00000010,	SRFL_PRHEX|SRFL_MORE,	SROM4_BFL2,	0xffff},
	{"",		0,		0,		SROM4_BFL3,		0xffff},
	{"boardflags2", 0x000000e0,	SRFL_PRHEX|SRFL_MORE,	SROM5_BFL2,	0xffff},
	{"",		0,		0,		SROM5_BFL3,		0xffff},
	{"boardflags2", 0xffffff00,	SRFL_PRHEX|SRFL_MORE,	SROM8_BFL2,	0xffff},
	{"",		0,		0,		SROM8_BFL3,		0xffff},
	{"boardtype",	0xfffffffc,	SRFL_PRHEX,	SROM_SSID,		0xffff},
	{"subvid",	0xfffffffc,	SRFL_PRHEX,	SROM_SVID,		0xffff},
	{"boardnum",	0x00000006,	0,		SROM_MACLO_IL0,		0xffff},
	{"boardnum",	0x00000008,	0,		SROM3_MACLO,		0xffff},
	{"boardnum",	0x00000010,	0,		SROM4_MACLO,		0xffff},
	{"boardnum",	0x000000e0,	0,		SROM5_MACLO,		0xffff},
	{"boardnum",	0x00000700,	0,		SROM8_MACLO,		0xffff},
	{"cc",		0x00000002,	0,		SROM_AABREV,		SROM_CC_MASK},
	{"regrev",	0x00000008,	0,		SROM_OPO,		0xff00},
	{"regrev",	0x00000010,	0,		SROM4_REGREV,		0x00ff},
	{"regrev",	0x000000e0,	0,		SROM5_REGREV,		0x00ff},
	{"regrev",	0x00000700,	0,		SROM8_REGREV,		0x00ff},
	{"ledbh0",	0x0000000e,	SRFL_NOFFS,	SROM_LEDBH10,		0x00ff},
	{"ledbh1",	0x0000000e,	SRFL_NOFFS,	SROM_LEDBH10,		0xff00},
	{"ledbh2",	0x0000000e,	SRFL_NOFFS,	SROM_LEDBH32,		0x00ff},
	{"ledbh3",	0x0000000e,	SRFL_NOFFS,	SROM_LEDBH32,		0xff00},
	{"ledbh0",	0x00000010,	SRFL_NOFFS,	SROM4_LEDBH10,		0x00ff},
	{"ledbh1",	0x00000010,	SRFL_NOFFS,	SROM4_LEDBH10,		0xff00},
	{"ledbh2",	0x00000010,	SRFL_NOFFS,	SROM4_LEDBH32,		0x00ff},
	{"ledbh3",	0x00000010,	SRFL_NOFFS,	SROM4_LEDBH32,		0xff00},
	{"ledbh0",	0x000000e0,	SRFL_NOFFS,	SROM5_LEDBH10,		0x00ff},
	{"ledbh1",	0x000000e0,	SRFL_NOFFS,	SROM5_LEDBH10,		0xff00},
	{"ledbh2",	0x000000e0,	SRFL_NOFFS,	SROM5_LEDBH32,		0x00ff},
	{"ledbh3",	0x000000e0,	SRFL_NOFFS,	SROM5_LEDBH32,		0xff00},
	{"ledbh0",	0x00000700,	SRFL_NOFFS,	SROM8_LEDBH10,		0x00ff},
	{"ledbh1",	0x00000700,	SRFL_NOFFS,	SROM8_LEDBH10,		0xff00},
	{"ledbh2",	0x00000700,	SRFL_NOFFS,	SROM8_LEDBH32,		0x00ff},
	{"ledbh3",	0x00000700,	SRFL_NOFFS,	SROM8_LEDBH32,		0xff00},
	{"pa0b0",	0x0000000e,	SRFL_PRHEX,	SROM_WL0PAB0,		0xffff},
	{"pa0b1",	0x0000000e,	SRFL_PRHEX,	SROM_WL0PAB1,		0xffff},
	{"pa0b2",	0x0000000e,	SRFL_PRHEX,	SROM_WL0PAB2,		0xffff},
	{"pa0itssit",	0x0000000e,	0,		SROM_ITT,		0x00ff},
	{"pa0maxpwr",	0x0000000e,	0,		SROM_WL10MAXP,		0x00ff},
	{"pa0b0",	0x00000700,	SRFL_PRHEX,	SROM8_W0_PAB0,		0xffff},
	{"pa0b1",	0x00000700,	SRFL_PRHEX,	SROM8_W0_PAB1,		0xffff},
	{"pa0b2",	0x00000700,	SRFL_PRHEX,	SROM8_W0_PAB2,		0xffff},
	{"pa0itssit",	0x00000700,	0,		SROM8_W0_ITTMAXP,	0xff00},
	{"pa0maxpwr",	0x00000700,	0,		SROM8_W0_ITTMAXP,	0x00ff},
	{"opo",		0x0000000c,	0,		SROM_OPO,		0x00ff},
	{"opo",		0x00000700,	0,		SROM8_2G_OFDMPO,	0x00ff},
	{"aa2g",	0x0000000e,	0,		SROM_AABREV,		SROM_AA0_MASK},
	{"aa2g",	0x000000f0,	0,		SROM4_AA,		0x00ff},
	{"aa2g",	0x00000700,	0,		SROM8_AA,		0x00ff},
	{"aa5g",	0x0000000e,	0,		SROM_AABREV,		SROM_AA1_MASK},
	{"aa5g",	0x000000f0,	0,		SROM4_AA,		0xff00},
	{"aa5g",	0x00000700,	0,		SROM8_AA,		0xff00},
	{"ag0",		0x0000000e,	0,		SROM_AG10,		0x00ff},
	{"ag1",		0x0000000e,	0,		SROM_AG10,		0xff00},
	{"ag0",		0x000000f0,	0,		SROM4_AG10,		0x00ff},
	{"ag1",		0x000000f0,	0,		SROM4_AG10,		0xff00},
	{"ag2",		0x000000f0,	0,		SROM4_AG32,		0x00ff},
	{"ag3",		0x000000f0,	0,		SROM4_AG32,		0xff00},
	{"ag0",		0x00000700,	0,		SROM8_AG10,		0x00ff},
	{"ag1",		0x00000700,	0,		SROM8_AG10,		0xff00},
	{"ag2",		0x00000700,	0,		SROM8_AG32,		0x00ff},
	{"ag3",		0x00000700,	0,		SROM8_AG32,		0xff00},
	{"pa1b0",	0x0000000e,	SRFL_PRHEX,	SROM_WL1PAB0,		0xffff},
	{"pa1b1",	0x0000000e,	SRFL_PRHEX,	SROM_WL1PAB1,		0xffff},
	{"pa1b2",	0x0000000e,	SRFL_PRHEX,	SROM_WL1PAB2,		0xffff},
	{"pa1lob0",	0x0000000c,	SRFL_PRHEX,	SROM_WL1LPAB0,		0xffff},
	{"pa1lob1",	0x0000000c,	SRFL_PRHEX,	SROM_WL1LPAB1,		0xffff},
	{"pa1lob2",	0x0000000c,	SRFL_PRHEX,	SROM_WL1LPAB2,		0xffff},
	{"pa1hib0",	0x0000000c,	SRFL_PRHEX,	SROM_WL1HPAB0,		0xffff},
	{"pa1hib1",	0x0000000c,	SRFL_PRHEX,	SROM_WL1HPAB1,		0xffff},
	{"pa1hib2",	0x0000000c,	SRFL_PRHEX,	SROM_WL1HPAB2,		0xffff},
	{"pa1itssit",	0x0000000e,	0,		SROM_ITT,		0xff00},
	{"pa1maxpwr",	0x0000000e,	0,		SROM_WL10MAXP,		0xff00},
	{"pa1lomaxpwr",	0x0000000c,	0,		SROM_WL1LHMAXP,		0xff00},
	{"pa1himaxpwr",	0x0000000c,	0,		SROM_WL1LHMAXP,		0x00ff},
	{"pa1b0",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB0,		0xffff},
	{"pa1b1",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB1,		0xffff},
	{"pa1b2",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB2,		0xffff},
	{"pa1lob0",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB0_LC,	0xffff},
	{"pa1lob1",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB1_LC,	0xffff},
	{"pa1lob2",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB2_LC,	0xffff},
	{"pa1hib0",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB0_HC,	0xffff},
	{"pa1hib1",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB1_HC,	0xffff},
	{"pa1hib2",	0x00000700,	SRFL_PRHEX,	SROM8_W1_PAB2_HC,	0xffff},
	{"pa1itssit",	0x00000700,	0,		SROM8_W1_ITTMAXP,	0xff00},
	{"pa1maxpwr",	0x00000700,	0,		SROM8_W1_ITTMAXP,	0x00ff},
	{"pa1lomaxpwr",	0x00000700,	0,		SROM8_W1_MAXP_LCHC,	0xff00},
	{"pa1himaxpwr",	0x00000700,	0,		SROM8_W1_MAXP_LCHC,	0x00ff},
	{"bxa2g",	0x00000008,	0,		SROM_BXARSSI2G,		0x1800},
	{"rssisav2g",	0x00000008,	0,		SROM_BXARSSI2G,		0x0700},
	{"rssismc2g",	0x00000008,	0,		SROM_BXARSSI2G,		0x00f0},
	{"rssismf2g",	0x00000008,	0,		SROM_BXARSSI2G,		0x000f},
	{"bxa2g",	0x00000700,	0,		SROM8_BXARSSI2G,	0x1800},
	{"rssisav2g",	0x00000700,	0,		SROM8_BXARSSI2G,	0x0700},
	{"rssismc2g",	0x00000700,	0,		SROM8_BXARSSI2G,	0x00f0},
	{"rssismf2g",	0x00000700,	0,		SROM8_BXARSSI2G,	0x000f},
	{"bxa5g",	0x00000008,	0,		SROM_BXARSSI5G,		0x1800},
	{"rssisav5g",	0x00000008,	0,		SROM_BXARSSI5G,		0x0700},
	{"rssismc5g",	0x00000008,	0,		SROM_BXARSSI5G,		0x00f0},
	{"rssismf5g",	0x00000008,	0,		SROM_BXARSSI5G,		0x000f},
	{"bxa5g",	0x00000700,	0,		SROM8_BXARSSI5G,	0x1800},
	{"rssisav5g",	0x00000700,	0,		SROM8_BXARSSI5G,	0x0700},
	{"rssismc5g",	0x00000700,	0,		SROM8_BXARSSI5G,	0x00f0},
	{"rssismf5g",	0x00000700,	0,		SROM8_BXARSSI5G,	0x000f},
	{"tri2g",	0x00000008,	0,		SROM_TRI52G,		0x00ff},
	{"tri5g",	0x00000008,	0,		SROM_TRI52G,		0xff00},
	{"tri5gl",	0x00000008,	0,		SROM_TRI5GHL,		0x00ff},
	{"tri5gh",	0x00000008,	0,		SROM_TRI5GHL,		0xff00},
	{"tri2g",	0x00000700,	0,		SROM8_TRI52G,		0x00ff},
	{"tri5g",	0x00000700,	0,		SROM8_TRI52G,		0xff00},
	{"tri5gl",	0x00000700,	0,		SROM8_TRI5GHL,		0x00ff},
	{"tri5gh",	0x00000700,	0,		SROM8_TRI5GHL,		0xff00},
	{"rxpo2g",	0x00000008,	SRFL_PRSIGN,	SROM_RXPO52G,		0x00ff},
	{"rxpo5g",	0x00000008,	SRFL_PRSIGN,	SROM_RXPO52G,		0xff00},
	{"rxpo2g",	0x00000700,	SRFL_PRSIGN,	SROM8_RXPO52G,		0x00ff},
	{"rxpo5g",	0x00000700,	SRFL_PRSIGN,	SROM8_RXPO52G,		0xff00},
	{"txchain",	0x000000f0,	SRFL_NOFFS,	SROM4_TXRXC,		SROM4_TXCHAIN_MASK},
	{"rxchain",	0x000000f0,	SRFL_NOFFS,	SROM4_TXRXC,		SROM4_RXCHAIN_MASK},
	{"antswitch",	0x000000f0,	SRFL_NOFFS,	SROM4_TXRXC,		SROM4_SWITCH_MASK},
	{"txchain",	0x00000700,	SRFL_NOFFS,	SROM8_TXRXC,		SROM4_TXCHAIN_MASK},
	{"rxchain",	0x00000700,	SRFL_NOFFS,	SROM8_TXRXC,		SROM4_RXCHAIN_MASK},
	{"antswitch",	0x00000700,	SRFL_NOFFS,	SROM8_TXRXC,		SROM4_SWITCH_MASK},
	{"tssipos2g",	0x00000700,	0,		SROM8_FEM2G,	SROM8_FEM_TSSIPOS_MASK},
	{"extpagain2g",	0x00000700,	0,		SROM8_FEM2G,	SROM8_FEM_EXTPA_GAIN_MASK},
	{"pdetrange2g",	0x00000700,	0,		SROM8_FEM2G,	SROM8_FEM_PDET_RANGE_MASK},
	{"triso2g",	0x00000700,	0,		SROM8_FEM2G,	SROM8_FEM_TR_ISO_MASK},
	{"antswctl2g",	0x00000700,	0,		SROM8_FEM2G,	SROM8_FEM_ANTSWLUT_MASK},
	{"tssipos5g",	0x00000700,	0,		SROM8_FEM5G,	SROM8_FEM_TSSIPOS_MASK},
	{"extpagain5g",	0x00000700,	0,		SROM8_FEM5G,	SROM8_FEM_EXTPA_GAIN_MASK},
	{"pdetrange5g",	0x00000700,	0,		SROM8_FEM5G,	SROM8_FEM_PDET_RANGE_MASK},
	{"triso5g",	0x00000700,	0,		SROM8_FEM5G,	SROM8_FEM_TR_ISO_MASK},
	{"antswctl5g",	0x00000700,	0,		SROM8_FEM5G,	SROM8_FEM_ANTSWLUT_MASK},
	{"txpid2ga0",	0x000000f0,	0,		SROM4_TXPID2G,		0x00ff},
	{"txpid2ga1",	0x000000f0,	0,		SROM4_TXPID2G,		0xff00},
	{"txpid2ga2",	0x000000f0,	0,		SROM4_TXPID2G + 1,	0x00ff},
	{"txpid2ga3",	0x000000f0,	0,		SROM4_TXPID2G + 1,	0xff00},
	{"txpid5ga0",	0x000000f0,	0,		SROM4_TXPID5G,		0x00ff},
	{"txpid5ga1",	0x000000f0,	0,		SROM4_TXPID5G,		0xff00},
	{"txpid5ga2",	0x000000f0,	0,		SROM4_TXPID5G + 1,	0x00ff},
	{"txpid5ga3",	0x000000f0,	0,		SROM4_TXPID5G + 1,	0xff00},
	{"txpid5gla0",	0x000000f0,	0,		SROM4_TXPID5GL,		0x00ff},
	{"txpid5gla1",	0x000000f0,	0,		SROM4_TXPID5GL,		0xff00},
	{"txpid5gla2",	0x000000f0,	0,		SROM4_TXPID5GL + 1,	0x00ff},
	{"txpid5gla3",	0x000000f0,	0,		SROM4_TXPID5GL + 1,	0xff00},
	{"txpid5gha0",	0x000000f0,	0,		SROM4_TXPID5GH,		0x00ff},
	{"txpid5gha1",	0x000000f0,	0,		SROM4_TXPID5GH,		0xff00},
	{"txpid5gha2",	0x000000f0,	0,		SROM4_TXPID5GH + 1,	0x00ff},
	{"txpid5gha3",	0x000000f0,	0,		SROM4_TXPID5GH + 1,	0xff00},

	{"ccode",	0x0000000f,	SRFL_CCODE,	SROM_CCODE,		0xffff},
	{"ccode",	0x00000010,	SRFL_CCODE,	SROM4_CCODE,		0xffff},
	{"ccode",	0x000000e0,	SRFL_CCODE,	SROM5_CCODE,		0xffff},
	{"ccode",	0x00000700,	SRFL_CCODE,	SROM8_CCODE,		0xffff},
	{"macaddr",	0x00000700,	SRFL_ETHADDR,	SROM8_MACHI,		0xffff},
	{"macaddr",	0x000000e0,	SRFL_ETHADDR,	SROM5_MACHI,		0xffff},
	{"macaddr",	0x00000010,	SRFL_ETHADDR,	SROM4_MACHI,		0xffff},
	{"macaddr",	0x00000008,	SRFL_ETHADDR,	SROM3_MACHI,		0xffff},
	{"il0macaddr",	0x00000007,	SRFL_ETHADDR,	SROM_MACHI_IL0,		0xffff},
	{"et1macaddr",	0x00000007,	SRFL_ETHADDR,	SROM_MACHI_ET1,		0xffff},
	{"leddc",	0x00000700,	SRFL_NOFFS|SRFL_LEDDC,	SROM8_LEDDC,	0xffff},
	{"leddc",	0x000000e0,	SRFL_NOFFS|SRFL_LEDDC,	SROM5_LEDDC,	0xffff},
	{"leddc",	0x00000010,	SRFL_NOFFS|SRFL_LEDDC,	SROM4_LEDDC,	0xffff},
	{"leddc",	0x00000008,	SRFL_NOFFS|SRFL_LEDDC,	SROM3_LEDDC,	0xffff},

	{"tempthresh",	0x00000700,	0,		SROM8_THERMAL,		0xff00},
	{"tempoffset",	0x00000700,	0,		SROM8_THERMAL,		0x00ff},
	{"rawtempsense", 0x00000700,	SRFL_PRHEX,	SROM8_MPWR_RAWTS,	0x01ff},
	{"measpower",	0x00000700,	SRFL_PRHEX,	SROM8_MPWR_RAWTS,	0xfe00},
	{"tempsense_slope",	0x00000700,	SRFL_PRHEX, 	SROM8_TS_SLP_OPT_CORRX,	0x00ff},
	{"tempcorrx",	0x00000700,	SRFL_PRHEX, 	SROM8_TS_SLP_OPT_CORRX,	0xfc00},
	{"tempsense_option",	0x00000700,	SRFL_PRHEX,	SROM8_TS_SLP_OPT_CORRX,	0x0300},
	{"freqoffset_corr",	0x00000700,	SRFL_PRHEX,	SROM8_FOC_HWIQ_IQSWP,	0x000f},
	{"iqcal_swp_dis",	0x00000700,	SRFL_PRHEX,	SROM8_FOC_HWIQ_IQSWP,	0x0010},
	{"hw_iqcal_en",	0x00000700,	SRFL_PRHEX,	SROM8_FOC_HWIQ_IQSWP,	0x0020},
	{"elna2g",      0x00000700,     0,              SROM8_EXTLNAGAIN,       0x00ff},
	{"elna5g",      0x00000700,     0,              SROM8_EXTLNAGAIN,       0xff00},
	{"phycal_tempdelta",	0x00000700,	0,	SROM8_PHYCAL_TEMPDELTA,	0x00ff},
	{"temps_period",	0x00000700,	0,	SROM8_PHYCAL_TEMPDELTA,	0x0f00},
	{"temps_hysteresis",	0x00000700,	0,	SROM8_PHYCAL_TEMPDELTA,	0xf000},
	{"measpower1", 0x00000700,	SRFL_PRHEX, SROM8_MPWR_1_AND_2, 	0x007f},
	{"measpower2",	0x00000700, 	SRFL_PRHEX, SROM8_MPWR_1_AND_2, 	0x3f80},

	{"cck2gpo",	0x000000f0,	0,		SROM4_2G_CCKPO,		0xffff},
	{"cck2gpo",	0x00000100,	0,		SROM8_2G_CCKPO,		0xffff},
	{"ofdm2gpo",	0x000000f0,	SRFL_MORE,	SROM4_2G_OFDMPO,	0xffff},
	{"",		0,		0,		SROM4_2G_OFDMPO + 1,	0xffff},
	{"ofdm5gpo",	0x000000f0,	SRFL_MORE,	SROM4_5G_OFDMPO,	0xffff},
	{"",		0,		0,		SROM4_5G_OFDMPO + 1,	0xffff},
	{"ofdm5glpo",	0x000000f0,	SRFL_MORE,	SROM4_5GL_OFDMPO,	0xffff},
	{"",		0,		0,		SROM4_5GL_OFDMPO + 1,	0xffff},
	{"ofdm5ghpo",	0x000000f0,	SRFL_MORE,	SROM4_5GH_OFDMPO,	0xffff},
	{"",		0,		0,		SROM4_5GH_OFDMPO + 1,	0xffff},
	{"ofdm2gpo",	0x00000100,	SRFL_MORE,	SROM8_2G_OFDMPO,	0xffff},
	{"",		0,		0,		SROM8_2G_OFDMPO + 1,	0xffff},
	{"ofdm5gpo",	0x00000100,	SRFL_MORE,	SROM8_5G_OFDMPO,	0xffff},
	{"",		0,		0,		SROM8_5G_OFDMPO + 1,	0xffff},
	{"ofdm5glpo",	0x00000100,	SRFL_MORE,	SROM8_5GL_OFDMPO,	0xffff},
	{"",		0,		0,		SROM8_5GL_OFDMPO + 1,	0xffff},
	{"ofdm5ghpo",	0x00000100,	SRFL_MORE,	SROM8_5GH_OFDMPO,	0xffff},
	{"",		0,		0,		SROM8_5GH_OFDMPO + 1,	0xffff},
	{"mcs2gpo0",	0x000000f0,	0,		SROM4_2G_MCSPO,		0xffff},
	{"mcs2gpo1",	0x000000f0,	0,		SROM4_2G_MCSPO + 1,	0xffff},
	{"mcs2gpo2",	0x000000f0,	0,		SROM4_2G_MCSPO + 2,	0xffff},
	{"mcs2gpo3",	0x000000f0,	0,		SROM4_2G_MCSPO + 3,	0xffff},
	{"mcs2gpo4",	0x000000f0,	0,		SROM4_2G_MCSPO + 4,	0xffff},
	{"mcs2gpo5",	0x000000f0,	0,		SROM4_2G_MCSPO + 5,	0xffff},
	{"mcs2gpo6",	0x000000f0,	0,		SROM4_2G_MCSPO + 6,	0xffff},
	{"mcs2gpo7",	0x000000f0,	0,		SROM4_2G_MCSPO + 7,	0xffff},
	{"mcs5gpo0",	0x000000f0,	0,		SROM4_5G_MCSPO,		0xffff},
	{"mcs5gpo1",	0x000000f0,	0,		SROM4_5G_MCSPO + 1,	0xffff},
	{"mcs5gpo2",	0x000000f0,	0,		SROM4_5G_MCSPO + 2,	0xffff},
	{"mcs5gpo3",	0x000000f0,	0,		SROM4_5G_MCSPO + 3,	0xffff},
	{"mcs5gpo4",	0x000000f0,	0,		SROM4_5G_MCSPO + 4,	0xffff},
	{"mcs5gpo5",	0x000000f0,	0,		SROM4_5G_MCSPO + 5,	0xffff},
	{"mcs5gpo6",	0x000000f0,	0,		SROM4_5G_MCSPO + 6,	0xffff},
	{"mcs5gpo7",	0x000000f0,	0,		SROM4_5G_MCSPO + 7,	0xffff},
	{"mcs5glpo0",	0x000000f0,	0,		SROM4_5GL_MCSPO,	0xffff},
	{"mcs5glpo1",	0x000000f0,	0,		SROM4_5GL_MCSPO + 1,	0xffff},
	{"mcs5glpo2",	0x000000f0,	0,		SROM4_5GL_MCSPO + 2,	0xffff},
	{"mcs5glpo3",	0x000000f0,	0,		SROM4_5GL_MCSPO + 3,	0xffff},
	{"mcs5glpo4",	0x000000f0,	0,		SROM4_5GL_MCSPO + 4,	0xffff},
	{"mcs5glpo5",	0x000000f0,	0,		SROM4_5GL_MCSPO + 5,	0xffff},
	{"mcs5glpo6",	0x000000f0,	0,		SROM4_5GL_MCSPO + 6,	0xffff},
	{"mcs5glpo7",	0x000000f0,	0,		SROM4_5GL_MCSPO + 7,	0xffff},
	{"mcs5ghpo0",	0x000000f0,	0,		SROM4_5GH_MCSPO,	0xffff},
	{"mcs5ghpo1",	0x000000f0,	0,		SROM4_5GH_MCSPO + 1,	0xffff},
	{"mcs5ghpo2",	0x000000f0,	0,		SROM4_5GH_MCSPO + 2,	0xffff},
	{"mcs5ghpo3",	0x000000f0,	0,		SROM4_5GH_MCSPO + 3,	0xffff},
	{"mcs5ghpo4",	0x000000f0,	0,		SROM4_5GH_MCSPO + 4,	0xffff},
	{"mcs5ghpo5",	0x000000f0,	0,		SROM4_5GH_MCSPO + 5,	0xffff},
	{"mcs5ghpo6",	0x000000f0,	0,		SROM4_5GH_MCSPO + 6,	0xffff},
	{"mcs5ghpo7",	0x000000f0,	0,		SROM4_5GH_MCSPO + 7,	0xffff},
	{"mcs2gpo0",	0x00000100,	0,		SROM8_2G_MCSPO,		0xffff},
	{"mcs2gpo1",	0x00000100,	0,		SROM8_2G_MCSPO + 1,	0xffff},
	{"mcs2gpo2",	0x00000100,	0,		SROM8_2G_MCSPO + 2,	0xffff},
	{"mcs2gpo3",	0x00000100,	0,		SROM8_2G_MCSPO + 3,	0xffff},
	{"mcs2gpo4",	0x00000100,	0,		SROM8_2G_MCSPO + 4,	0xffff},
	{"mcs2gpo5",	0x00000100,	0,		SROM8_2G_MCSPO + 5,	0xffff},
	{"mcs2gpo6",	0x00000100,	0,		SROM8_2G_MCSPO + 6,	0xffff},
	{"mcs2gpo7",	0x00000100,	0,		SROM8_2G_MCSPO + 7,	0xffff},
	{"mcs5gpo0",	0x00000100,	0,		SROM8_5G_MCSPO,		0xffff},
	{"mcs5gpo1",	0x00000100,	0,		SROM8_5G_MCSPO + 1,	0xffff},
	{"mcs5gpo2",	0x00000100,	0,		SROM8_5G_MCSPO + 2,	0xffff},
	{"mcs5gpo3",	0x00000100,	0,		SROM8_5G_MCSPO + 3,	0xffff},
	{"mcs5gpo4",	0x00000100,	0,		SROM8_5G_MCSPO + 4,	0xffff},
	{"mcs5gpo5",	0x00000100,	0,		SROM8_5G_MCSPO + 5,	0xffff},
	{"mcs5gpo6",	0x00000100,	0,		SROM8_5G_MCSPO + 6,	0xffff},
	{"mcs5gpo7",	0x00000100,	0,		SROM8_5G_MCSPO + 7,	0xffff},
	{"mcs5glpo0",	0x00000100,	0,		SROM8_5GL_MCSPO,	0xffff},
	{"mcs5glpo1",	0x00000100,	0,		SROM8_5GL_MCSPO + 1,	0xffff},
	{"mcs5glpo2",	0x00000100,	0,		SROM8_5GL_MCSPO + 2,	0xffff},
	{"mcs5glpo3",	0x00000100,	0,		SROM8_5GL_MCSPO + 3,	0xffff},
	{"mcs5glpo4",	0x00000100,	0,		SROM8_5GL_MCSPO + 4,	0xffff},
	{"mcs5glpo5",	0x00000100,	0,		SROM8_5GL_MCSPO + 5,	0xffff},
	{"mcs5glpo6",	0x00000100,	0,		SROM8_5GL_MCSPO + 6,	0xffff},
	{"mcs5glpo7",	0x00000100,	0,		SROM8_5GL_MCSPO + 7,	0xffff},
	{"mcs5ghpo0",	0x00000100,	0,		SROM8_5GH_MCSPO,	0xffff},
	{"mcs5ghpo1",	0x00000100,	0,		SROM8_5GH_MCSPO + 1,	0xffff},
	{"mcs5ghpo2",	0x00000100,	0,		SROM8_5GH_MCSPO + 2,	0xffff},
	{"mcs5ghpo3",	0x00000100,	0,		SROM8_5GH_MCSPO + 3,	0xffff},
	{"mcs5ghpo4",	0x00000100,	0,		SROM8_5GH_MCSPO + 4,	0xffff},
	{"mcs5ghpo5",	0x00000100,	0,		SROM8_5GH_MCSPO + 5,	0xffff},
	{"mcs5ghpo6",	0x00000100,	0,		SROM8_5GH_MCSPO + 6,	0xffff},
	{"mcs5ghpo7",	0x00000100,	0,		SROM8_5GH_MCSPO + 7,	0xffff},
	{"cddpo",	0x000000f0,	0,		SROM4_CDDPO,		0xffff},
	{"stbcpo",	0x000000f0,	0,		SROM4_STBCPO,		0xffff},
	{"bw40po",	0x000000f0,	0,		SROM4_BW40PO,		0xffff},
	{"bwduppo",	0x000000f0,	0,		SROM4_BWDUPPO,		0xffff},
	{"cddpo",	0x00000100,	0,		SROM8_CDDPO,		0xffff},
	{"stbcpo",	0x00000100,	0,		SROM8_STBCPO,		0xffff},
	{"bw40po",	0x00000100,	0,		SROM8_BW40PO,		0xffff},
	{"bwduppo",	0x00000100,	0,		SROM8_BWDUPPO,		0xffff},

	/* power per rate from sromrev 9 */
	{"cckbw202gpo",		0x00000600,	0,	SROM9_2GPO_CCKBW20,		0xffff},
	{"cckbw20ul2gpo",	0x00000600,	0,	SROM9_2GPO_CCKBW20UL,		0xffff},
	{"legofdmbw202gpo",	0x00000600,	SRFL_MORE, SROM9_2GPO_LOFDMBW20,	0xffff},
	{"",			0,		0,	SROM9_2GPO_LOFDMBW20 + 1,	0xffff},
	{"legofdmbw20ul2gpo",	0x00000600,	SRFL_MORE, SROM9_2GPO_LOFDMBW20UL,	0xffff},
	{"",			0,		0,	SROM9_2GPO_LOFDMBW20UL + 1,	0xffff},
	{"legofdmbw205glpo",	0x00000600,	SRFL_MORE, SROM9_5GLPO_LOFDMBW20,	0xffff},
	{"",			0,		0,	SROM9_5GLPO_LOFDMBW20 + 1,	0xffff},
	{"legofdmbw20ul5glpo",	0x00000600,	SRFL_MORE, SROM9_5GLPO_LOFDMBW20UL,	0xffff},
	{"",			0,		0,	SROM9_5GLPO_LOFDMBW20UL + 1,	0xffff},
	{"legofdmbw205gmpo",	0x00000600,	SRFL_MORE, SROM9_5GMPO_LOFDMBW20,	0xffff},
	{"",			0,		0,	SROM9_5GMPO_LOFDMBW20 + 1,	0xffff},
	{"legofdmbw20ul5gmpo",	0x00000600,	SRFL_MORE, SROM9_5GMPO_LOFDMBW20UL,	0xffff},
	{"",			0,		0,	SROM9_5GMPO_LOFDMBW20UL + 1,	0xffff},
	{"legofdmbw205ghpo",	0x00000600,	SRFL_MORE, SROM9_5GHPO_LOFDMBW20,	0xffff},
	{"",			0,		0,	SROM9_5GHPO_LOFDMBW20 + 1,	0xffff},
	{"legofdmbw20ul5ghpo",	0x00000600,	SRFL_MORE, SROM9_5GHPO_LOFDMBW20UL,	0xffff},
	{"",			0,		0,	SROM9_5GHPO_LOFDMBW20UL + 1,	0xffff},
	{"mcsbw202gpo",		0x00000600,	SRFL_MORE, SROM9_2GPO_MCSBW20,		0xffff},
	{"",			0,		0,	SROM9_2GPO_MCSBW20 + 1,		0xffff},
	{"mcsbw20ul2gpo",      	0x00000600,	SRFL_MORE, SROM9_2GPO_MCSBW20UL,	0xffff},
	{"",			0,		0,	SROM9_2GPO_MCSBW20UL + 1,	0xffff},
	{"mcsbw402gpo",		0x00000600,	SRFL_MORE, SROM9_2GPO_MCSBW40,		0xffff},
	{"",			0,		0,	SROM9_2GPO_MCSBW40 + 1,		0xffff},
	{"mcsbw205glpo",	0x00000600,	SRFL_MORE, SROM9_5GLPO_MCSBW20,		0xffff},
	{"",			0,		0,	SROM9_5GLPO_MCSBW20 + 1,	0xffff},
	{"mcsbw20ul5glpo",	0x00000600,	SRFL_MORE, SROM9_5GLPO_MCSBW20UL,	0xffff},
	{"",			0,		0,	SROM9_5GLPO_MCSBW20UL + 1,	0xffff},
	{"mcsbw405glpo",	0x00000600,	SRFL_MORE, SROM9_5GLPO_MCSBW40,		0xffff},
	{"",			0,		0,	SROM9_5GLPO_MCSBW40 + 1,	0xffff},
	{"mcsbw205gmpo",	0x00000600,	SRFL_MORE, SROM9_5GMPO_MCSBW20,		0xffff},
	{"",			0,		0,	SROM9_5GMPO_MCSBW20 + 1,	0xffff},
	{"mcsbw20ul5gmpo",	0x00000600,	SRFL_MORE, SROM9_5GMPO_MCSBW20UL,	0xffff},
	{"",			0,		0,	SROM9_5GMPO_MCSBW20UL + 1,	0xffff},
	{"mcsbw405gmpo",	0x00000600,	SRFL_MORE, SROM9_5GMPO_MCSBW40,		0xffff},
	{"",			0,		0,	SROM9_5GMPO_MCSBW40 + 1,	0xffff},
	{"mcsbw205ghpo",	0x00000600,	SRFL_MORE, SROM9_5GHPO_MCSBW20,		0xffff},
	{"",			0,		0,	SROM9_5GHPO_MCSBW20 + 1,	0xffff},
	{"mcsbw20ul5ghpo",	0x00000600,	SRFL_MORE, SROM9_5GHPO_MCSBW20UL,	0xffff},
	{"",			0,		0,	SROM9_5GHPO_MCSBW20UL + 1,	0xffff},
	{"mcsbw405ghpo",	0x00000600,	SRFL_MORE, SROM9_5GHPO_MCSBW40,		0xffff},
	{"",			0,		0,	SROM9_5GHPO_MCSBW40 + 1,	0xffff},
	{"mcs32po",		0x00000600,	0,	SROM9_PO_MCS32,			0xffff},
	{"legofdm40duppo",	0x00000600,	0,	SROM9_PO_LOFDM40DUP,	0xffff},
	{"pcieingress_war",	0x00000700,	0,	SROM8_PCIEINGRESS_WAR,	0xf},
	{"rxgainerr2ga0",	0x00000700,	0,	SROM8_RXGAINERR_2G,		0x003f},
	{"rxgainerr2ga1",	0x00000700,	0,	SROM8_RXGAINERR_2G,		0x07c0},
	{"rxgainerr2ga2",	0x00000700,	0,	SROM8_RXGAINERR_2G,		0xf800},
	{"rxgainerr5gla0",	0x00000700,	0,	SROM8_RXGAINERR_5GL,	0x003f},
	{"rxgainerr5gla1",	0x00000700,	0,	SROM8_RXGAINERR_5GL,	0x07c0},
	{"rxgainerr5gla2",	0x00000700,	0,	SROM8_RXGAINERR_5GL,	0xf800},
	{"rxgainerr5gma0",	0x00000700,	0,	SROM8_RXGAINERR_5GM,	0x003f},
	{"rxgainerr5gma1",	0x00000700,	0,	SROM8_RXGAINERR_5GM,	0x07c0},
	{"rxgainerr5gma2",	0x00000700,	0,	SROM8_RXGAINERR_5GM,	0xf800},
	{"rxgainerr5gha0",	0x00000700,	0,	SROM8_RXGAINERR_5GH,	0x003f},
	{"rxgainerr5gha1",	0x00000700,	0,	SROM8_RXGAINERR_5GH,	0x07c0},
	{"rxgainerr5gha2",	0x00000700,	0,	SROM8_RXGAINERR_5GH,	0xf800},
	{"rxgainerr5gua0",	0x00000700,	0,	SROM8_RXGAINERR_5GU,	0x003f},
	{"rxgainerr5gua1",	0x00000700,	0,	SROM8_RXGAINERR_5GU,	0x07c0},
	{"rxgainerr5gua2",	0x00000700,	0,	SROM8_RXGAINERR_5GU,	0xf800},
	{"sar2g",       	0x00000600,	0,	SROM9_SAR,          	0x00ff},
	{"sar5g",           0x00000600,	0,	SROM9_SAR,	            0xff00},
	{"noiselvl2ga0",	0x00000700,	0,	SROM8_NOISELVL_2G,		0x001f},
	{"noiselvl2ga1",	0x00000700,	0,	SROM8_NOISELVL_2G,		0x03e0},
	{"noiselvl2ga2",	0x00000700,	0,	SROM8_NOISELVL_2G,		0x7c00},
	{"noiselvl5gla0",	0x00000700,	0,	SROM8_NOISELVL_5GL,		0x001f},
	{"noiselvl5gla1",	0x00000700,	0,	SROM8_NOISELVL_5GL,		0x03e0},
	{"noiselvl5gla2",	0x00000700,	0,	SROM8_NOISELVL_5GL,		0x7c00},
	{"noiselvl5gma0",	0x00000700,	0,	SROM8_NOISELVL_5GM,		0x001f},
	{"noiselvl5gma1",	0x00000700,	0,	SROM8_NOISELVL_5GM,		0x03e0},
	{"noiselvl5gma2",	0x00000700,	0,	SROM8_NOISELVL_5GM,		0x7c00},
	{"noiselvl5gha0",	0x00000700,	0,	SROM8_NOISELVL_5GH,		0x001f},
	{"noiselvl5gha1",	0x00000700,	0,	SROM8_NOISELVL_5GH,		0x03e0},
	{"noiselvl5gha2",	0x00000700,	0,	SROM8_NOISELVL_5GH,		0x7c00},
	{"noiselvl5gua0",	0x00000700,	0,	SROM8_NOISELVL_5GU,		0x001f},
	{"noiselvl5gua1",	0x00000700,	0,	SROM8_NOISELVL_5GU,		0x03e0},
	{"noiselvl5gua2",	0x00000700,	0,	SROM8_NOISELVL_5GU,		0x7c00},
	{"noisecaloffset",	0x00000300,	0,	SROM8_NOISECALOFFSET,		0x00ff},
	{"noisecaloffset5g",	0x00000300,	0,	SROM8_NOISECALOFFSET,		0xff00},
	{"subband5gver",	0x00000700,	0,	SROM8_SUBBAND_PPR,		0x7},

	{"cckPwrOffset",	0x00000400,	0,	SROM10_CCKPWROFFSET,		0xffff},
	/* swctrlmap_2g array, note that the last element doesn't have SRFL_ARRAY flag set */
	{"swctrlmap_2g", 0x00000400, SRFL_MORE|SRFL_PRHEX|SRFL_ARRAY, SROM10_SWCTRLMAP_2G, 0xffff},
	{"",	0x00000400, SRFL_ARRAY,	SROM10_SWCTRLMAP_2G + 1,			0xffff},
	{"",	0x00000400, SRFL_MORE|SRFL_PRHEX|SRFL_ARRAY, SROM10_SWCTRLMAP_2G + 2, 	0xffff},
	{"",	0x00000400, SRFL_ARRAY,	SROM10_SWCTRLMAP_2G + 3,			0xffff},
	{"",	0x00000400, SRFL_MORE|SRFL_PRHEX|SRFL_ARRAY, SROM10_SWCTRLMAP_2G + 4,	0xffff},
	{"",	0x00000400, SRFL_ARRAY,	SROM10_SWCTRLMAP_2G + 5,			0xffff},
	{"",	0x00000400, SRFL_MORE|SRFL_PRHEX|SRFL_ARRAY, SROM10_SWCTRLMAP_2G + 6,	0xffff},
	{"",	0x00000400, SRFL_ARRAY,	SROM10_SWCTRLMAP_2G + 7,			0xffff},
	{"",	0x00000400, SRFL_PRHEX,	SROM10_SWCTRLMAP_2G + 8,			0xffff},

	/* sromrev 11 */
	{"boardflags3",	0xfffff800,	SRFL_PRHEX|SRFL_MORE,	SROM11_BFL4,	0xffff},
	{"",		0,		0,			SROM11_BFL5,	0xffff},
	{"boardnum",	0xfffff800,	0,			SROM11_MACLO,	0xffff},
	{"macaddr",	0xfffff800,	SRFL_ETHADDR,		SROM11_MACHI,	0xffff},
	{"ccode",	0xfffff800,	SRFL_CCODE,		SROM11_CCODE,	0xffff},
	{"regrev",	0xfffff800,	0,			SROM11_REGREV,	0x00ff},
	{"ledbh0",	0xfffff800,	SRFL_NOFFS,		SROM11_LEDBH10,	0x00ff},
	{"ledbh1",	0xfffff800,	SRFL_NOFFS,		SROM11_LEDBH10,	0xff00},
	{"ledbh2",	0xfffff800,	SRFL_NOFFS,		SROM11_LEDBH32,	0x00ff},
	{"ledbh3",	0xfffff800,	SRFL_NOFFS,		SROM11_LEDBH32,	0xff00},
	{"leddc",	0xfffff800,	SRFL_NOFFS|SRFL_LEDDC,	SROM11_LEDDC,	0xffff},
	{"aa2g",	0xfffff800,	0,			SROM11_AA,	0x00ff},
	{"aa5g",	0xfffff800,	0,			SROM11_AA,	0xff00},
	{"agbg0",	0xfffff800,	0,			SROM11_AGBG10,  0xff00},
	{"agbg1",	0xfffff800,	0,			SROM11_AGBG10,	0x00ff},
	{"agbg2",	0xfffff800,	0,			SROM11_AGBG2A0,	0xff00},
	{"aga0",	0xfffff800,	0,			SROM11_AGBG2A0,	0x00ff},
	{"aga1",	0xfffff800,	0,			SROM11_AGA21,   0xff00},
	{"aga2",	0xfffff800,	0,			SROM11_AGA21,	0x00ff},
	{"txchain",	0xfffff800,	SRFL_NOFFS,	SROM11_TXRXC,	SROM4_TXCHAIN_MASK},
	{"rxchain",	0xfffff800,	SRFL_NOFFS,	SROM11_TXRXC,	SROM4_RXCHAIN_MASK},
	{"antswitch",	0xfffff800,	SRFL_NOFFS,	SROM11_TXRXC,	SROM4_SWITCH_MASK},

	{"tssiposslope2g",	0xfffff800,	0,		SROM11_FEM_CFG1, 	0x0001},
	{"epagain2g",		0xfffff800,	0,		SROM11_FEM_CFG1, 	0x000e},
	{"pdgain2g",		0xfffff800,	0,		SROM11_FEM_CFG1, 	0x01f0},
	{"tworangetssi2g",	0xfffff800,	0,		SROM11_FEM_CFG1, 	0x0200},
	{"papdcap2g",		0xfffff800,	0,		SROM11_FEM_CFG1, 	0x0400},
	{"femctrl",		0xfffff800,	0,		SROM11_FEM_CFG1, 	0xf800},

	{"tssiposslope5g",	0xfffff800,	0,		SROM11_FEM_CFG2, 	0x0001},
	{"epagain5g",		0xfffff800,	0,		SROM11_FEM_CFG2, 	0x000e},
	{"pdgain5g",		0xfffff800,	0,		SROM11_FEM_CFG2, 	0x01f0},
	{"tworangetssi5g",	0xfffff800,	0,		SROM11_FEM_CFG2, 	0x0200},
	{"papdcap5g",		0xfffff800,	0,		SROM11_FEM_CFG2, 	0x0400},
	{"gainctrlsph",		0xfffff800,	0,		SROM11_FEM_CFG2, 	0xf800},

	{"tempthresh",		0xfffff800,	0,		SROM11_THERMAL,		0xff00},
	{"tempoffset",		0xfffff800,	0,		SROM11_THERMAL,		0x00ff},
	{"rawtempsense", 	0xfffff800,	SRFL_PRHEX,	SROM11_MPWR_RAWTS,	0x01ff},
	{"measpower",		0xfffff800,	SRFL_PRHEX,	SROM11_MPWR_RAWTS,	0xfe00},
	{"tempsense_slope",	0xfffff800,	SRFL_PRHEX, 	SROM11_TS_SLP_OPT_CORRX, 0x00ff},
	{"tempcorrx",		0xfffff800,	SRFL_PRHEX, 	SROM11_TS_SLP_OPT_CORRX, 0xfc00},
	{"tempsense_option",	0xfffff800,	SRFL_PRHEX,	SROM11_TS_SLP_OPT_CORRX, 0x0300},
	{"xtalfreq",		0xfffff800,	0,		SROM11_XTAL_FREQ, 	0xffff},
	/* Special PA Params for 4350 5G Band, 40/80 MHz BW Ant #1 */
	{"pa5gbw4080a1", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB0_4080_W0_A1, 0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB0_4080_W1_A1,                 0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB0_4080_W2_A1,                 0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB1_4080_W0_A1,                 0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_4080_PA,     0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_4080_PA + 1, 0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_4080_PA,     0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_4080_PA + 1, 0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_4080_PA + 2, 0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB3_4080_PA,     0xffff},
	{"", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB3_4080_PA + 1, 0xffff},
	{"", 0xfffff800, SRFL_PRHEX,              SROM11_PATH2 + SROM11_5GB3_4080_PA + 2, 0xffff},
	{"phycal_tempdelta",	0xfffff800,	0,		SROM11_PHYCAL_TEMPDELTA, 0x00ff},
	{"temps_period",	0xfffff800,	0,		SROM11_PHYCAL_TEMPDELTA, 0x0f00},
	{"temps_hysteresis",	0xfffff800,	0,		SROM11_PHYCAL_TEMPDELTA, 0xf000},
	{"measpower1", 		0xfffff800,	SRFL_PRHEX,	SROM11_MPWR_1_AND_2, 	0x007f},
	{"measpower2",		0xfffff800, 	SRFL_PRHEX,	SROM11_MPWR_1_AND_2, 	0x3f80},
	{"tssifloor2g",		0xfffff800,	SRFL_PRHEX,	SROM11_TSSIFLOOR_2G,	0x03ff},
	{"tssifloor5g",	0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_TSSIFLOOR_5GL,	0x03ff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_TSSIFLOOR_5GM,	0x03ff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_TSSIFLOOR_5GH,	0x03ff},
	{"",		0xfffff800,	SRFL_PRHEX,		SROM11_TSSIFLOOR_5GU,	0x03ff},
	{"pdoffset2g40ma0",     0xfffff800, 0,      SROM11_PDOFF_2G_40M,    0x000f},
	{"pdoffset2g40ma1",     0xfffff800, 0,      SROM11_PDOFF_2G_40M,    0x00f0},
	{"pdoffset2g40ma2",     0xfffff800, 0,      SROM11_PDOFF_2G_40M,    0x0f00},
	{"pdoffset2g40mvalid",  0xfffff800, 0,      SROM11_PDOFF_2G_40M,    0x8000},
	{"pdoffset40ma0",      	0xfffff800,	0,		SROM11_PDOFF_40M_A0,   	0xffff},
	{"pdoffset40ma1",      	0xfffff800,	0,		SROM11_PDOFF_40M_A1,   	0xffff},
	{"pdoffset40ma2",      	0xfffff800,	0,		SROM11_PDOFF_40M_A2,   	0xffff},
	{"pdoffset80ma0",      	0xfffff800,	0,		SROM11_PDOFF_80M_A0,   	0xffff},
	{"pdoffset80ma1",      	0xfffff800,	0,		SROM11_PDOFF_80M_A1,   	0xffff},
	{"pdoffset80ma2",      	0xfffff800,	0,		SROM11_PDOFF_80M_A2,   	0xffff},

	{"subband5gver",	0xfffff800, 	SRFL_PRHEX,	SROM11_SUBBAND5GVER, 	0xffff},
	{"paparambwver",	0xfffff800, 	0,		SROM11_MCSLR5GLPO, 	0xf000},
	/* Special PA Params for 4350 5G Band, 40/80 MHz BW Ant #0 */
	{"pa5gbw4080a0", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 +SROM11_5GB0_PA, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB0_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB0_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB3_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB3_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX,              SROM11_PATH2 + SROM11_5GB3_PA + 2, 0xffff},
	/* Special PA Params for 4335 5G Band, 40 MHz BW */
	{"pa5gbw40a0", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB0_PA, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB0_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB0_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB1_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB1_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB1_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB2_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB2_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB2_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB3_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_5GB3_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX,              SROM11_PATH1 + SROM11_5GB3_PA + 2, 0xffff},
	/* Special PA Params for 4335 5G Band, 80 MHz BW */
	{"pa5gbw80a0", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB0_PA, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB0_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB0_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB1_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB2_PA + 2, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB3_PA,     0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH2 + SROM11_5GB3_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX,              SROM11_PATH2 + SROM11_5GB3_PA + 2, 0xffff},
	/* Special PA Params for 4335 2G Band, CCK */
	{"pa2gccka0", 0xfffff800, SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_2G_PA, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_PATH1 + SROM11_2G_PA + 1, 0xffff},
	{"", 0xfffff800,	SRFL_PRHEX,              SROM11_PATH1 + SROM11_2G_PA + 2, 0xffff},

	/* power per rate */
	{"cckbw202gpo",		0xfffff800,	0,		SROM11_CCKBW202GPO, 	0xffff},
	{"cckbw20ul2gpo",	0xfffff800,	0,		SROM11_CCKBW20UL2GPO, 	0xffff},
	{"mcsbw202gpo",		0xfffff800,	SRFL_MORE,	SROM11_MCSBW202GPO,   	0xffff},
	{"",            	0xfffff800, 	0,          	SROM11_MCSBW202GPO_1, 	0xffff},
	{"mcsbw402gpo",		0xfffff800,	SRFL_MORE,	SROM11_MCSBW402GPO,   	0xffff},
	{"",            	0xfffff800, 	0,   		SROM11_MCSBW402GPO_1, 	0xffff},
	{"dot11agofdmhrbw202gpo", 0xfffff800, 	0, 	SROM11_DOT11AGOFDMHRBW202GPO, 	0xffff},
	{"ofdmlrbw202gpo",	0xfffff800, 	0, 		SROM11_OFDMLRBW202GPO,	0xffff},
	{"mcsbw205glpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW205GLPO, 	0xffff},
	{"",           		0xfffff800, 	0,   		SROM11_MCSBW205GLPO_1, 	0xffff},
	{"mcsbw405glpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW405GLPO, 	0xffff},
	{"",           		0xfffff800, 	0,     		SROM11_MCSBW405GLPO_1, 	0xffff},
	{"mcsbw805glpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW805GLPO, 	0xffff},
	{"",           		0xfffff800, 	0,    		SROM11_MCSBW805GLPO_1, 	0xffff},
	{"mcsbw205gmpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW205GMPO, 	0xffff},
	{"",           		0xfffff800, 	0,     		SROM11_MCSBW205GMPO_1, 	0xffff},
	{"mcsbw405gmpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW405GMPO, 	0xffff},
	{"",           		0xfffff800, 	0,     		SROM11_MCSBW405GMPO_1, 	0xffff},
	{"mcsbw805gmpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW805GMPO, 	0xffff},
	{"",           		0xfffff800, 	0,   		SROM11_MCSBW805GMPO_1, 	0xffff},
	{"mcsbw205ghpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW205GHPO, 	0xffff},
	{"",           		0xfffff800, 	0,  		SROM11_MCSBW205GHPO_1, 	0xffff},
	{"mcsbw405ghpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW405GHPO, 	0xffff},
	{"",           		0xfffff800, 	0,   		SROM11_MCSBW405GHPO_1, 	0xffff},
	{"mcsbw805ghpo",	0xfffff800,	SRFL_MORE,	SROM11_MCSBW805GHPO, 	0xffff},
	{"",           		0xfffff800, 	0,    		SROM11_MCSBW805GHPO_1, 	0xffff},
	{"mcslr5glpo",		0xfffff800,	0,		SROM11_MCSLR5GLPO, 	0x0fff},
	{"mcslr5gmpo",		0xfffff800,	0,		SROM11_MCSLR5GMPO, 	0xffff},
	{"mcslr5ghpo",		0xfffff800,	0,		SROM11_MCSLR5GHPO, 	0xffff},
	{"sb20in40hrpo", 	0xfffff800,	0,	SROM11_SB20IN40HRPO,		0xffff},
	{"sb20in80and160hr5glpo", 0xfffff800, 	0, 	SROM11_SB20IN80AND160HR5GLPO, 	0xffff},
	{"sb40and80hr5glpo",	  0xfffff800, 	0,	SROM11_SB40AND80HR5GLPO,	0xffff},
	{"sb20in80and160hr5gmpo", 0xfffff800, 	0,	SROM11_SB20IN80AND160HR5GMPO, 	0xffff},
	{"sb40and80hr5gmpo",	  0xfffff800, 	0,	SROM11_SB40AND80HR5GMPO,	0xffff},
	{"sb20in80and160hr5ghpo", 0xfffff800, 	0,	SROM11_SB20IN80AND160HR5GHPO, 	0xffff},
	{"sb40and80hr5ghpo",	  0xfffff800, 	0,	SROM11_SB40AND80HR5GHPO,	0xffff},
	{"sb20in40lrpo",	  0xfffff800, 	0,	SROM11_SB20IN40LRPO,		0xffff},
	{"sb20in80and160lr5glpo", 0xfffff800, 	0,	SROM11_SB20IN80AND160LR5GLPO, 	0xffff},
	{"sb40and80lr5glpo",	  0xfffff800, 	0,	SROM11_SB40AND80LR5GLPO,	0xffff},
	{"sb20in80and160lr5gmpo", 0xfffff800, 	0,	SROM11_SB20IN80AND160LR5GMPO, 	0xffff},
	{"sb40and80lr5gmpo",	  0xfffff800, 	0,	SROM11_SB40AND80LR5GMPO,	0xffff},
	{"sb20in80and160lr5ghpo", 0xfffff800, 	0,	SROM11_SB20IN80AND160LR5GHPO, 	0xffff},
	{"sb40and80lr5ghpo",	  0xfffff800, 	0,	SROM11_SB40AND80LR5GHPO,	0xffff},
	{"dot11agduphrpo",	  0xfffff800, 	0,	SROM11_DOT11AGDUPHRPO,		0xffff},
	{"dot11agduplrpo",	  0xfffff800, 	0,	SROM11_DOT11AGDUPLRPO,		0xffff},

	/* Misc */
	{"sar2g",       	0xfffff800,	0,	SROM11_SAR,          	0x00ff},
	{"sar5g",           	0xfffff800,	0,	SROM11_SAR,		0xff00},

	{"noiselvl2ga0",	0xfffff800,	0,		SROM11_NOISELVL_2G,	0x001f},
	{"noiselvl2ga1",	0xfffff800,	0,		SROM11_NOISELVL_2G,	0x03e0},
	{"noiselvl2ga2",	0xfffff800,	0,		SROM11_NOISELVL_2G,	0x7c00},
	{"noiselvl5ga0",	0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GL,	0x001f},
	{"",			0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GM,	0x001f},
	{"",			0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GH,	0x001f},
	{"",			0xfffff800,	0,		SROM11_NOISELVL_5GU,	0x001f},
	{"noiselvl5ga1",	0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GL,	0x03e0},
	{"",			0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GM,	0x03e0},
	{"",			0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GH,	0x03e0},
	{"",			0xfffff800,	0,		SROM11_NOISELVL_5GU,	0x03e0},
	{"noiselvl5ga2",	0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GL,	0x7c00},
	{"",			0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GM,	0x7c00},
	{"",			0xfffff800,	SRFL_ARRAY,	SROM11_NOISELVL_5GH,	0x7c00},
	{"",			0xfffff800,	0,		SROM11_NOISELVL_5GU,	0x7c00},

	{"rxgainerr2ga0", 	0xfffff800, 	0,    		SROM11_RXGAINERR_2G,    0x003f},
	{"rxgainerr2ga1", 	0xfffff800, 	0,    		SROM11_RXGAINERR_2G,    0x07c0},
	{"rxgainerr2ga2", 	0xfffff800, 	0,    		SROM11_RXGAINERR_2G,    0xf800},
	{"rxgainerr5ga0",      	0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GL,   0x003f},
	{"",      		0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GM,   0x003f},
	{"",      		0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GH,   0x003f},
	{"",      		0xfffff800, 	0,    		SROM11_RXGAINERR_5GU,   0x003f},
	{"rxgainerr5ga1",      	0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GL,   0x07c0},
	{"",      		0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GM,   0x07c0},
	{"",      		0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GH,   0x07c0},
	{"",      		0xfffff800, 	0,    		SROM11_RXGAINERR_5GU,   0x07c0},
	{"rxgainerr5ga2",      	0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GL,   0xf800},
	{"",      		0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GM,   0xf800},
	{"",      		0xfffff800, 	SRFL_ARRAY,    	SROM11_RXGAINERR_5GH,   0xf800},
	{"",      		0xfffff800, 	0,    		SROM11_RXGAINERR_5GU,   0xf800},
	{"rpcal2g",      	0xfffff800, 	0,		SROM11_RPCAL_2G,        0xffff},
	{"rpcal5gb0",      	0xfffff800, 	0,		SROM11_RPCAL_5GL,       0xffff},
	{"rpcal5gb1",      	0xfffff800, 	0,		SROM11_RPCAL_5GM,       0xffff},
	{"rpcal5gb2",      	0xfffff800, 	0,		SROM11_RPCAL_5GH,       0xffff},
	{"rpcal5gb3",      	0xfffff800, 	0,		SROM11_RPCAL_5GU,       0xffff},
	{"txidxcap2g",      	0xfffff800, 	0,		SROM11_TXIDXCAP2G,      0x0ff0},
	{"txidxcap5g",      	0xfffff800, 	0,		SROM11_TXIDXCAP5G,      0x0ff0},
	{"pdoffsetcckma0",      0xfffff800,     0,              SROM11_PDOFF_2G_CCK,    0x000f},
	{"pdoffsetcckma1",      0xfffff800,     0,              SROM11_PDOFF_2G_CCK,    0x00f0},
	{"pdoffsetcckma2",      0xfffff800,     0,              SROM11_PDOFF_2G_CCK,    0x0f00},
	{NULL,		0,		0,		0,			0}
};

static const sromvar_t perpath_pci_sromvars[] = {
	{"maxp2ga",	0x000000f0,	0,		SROM4_2G_ITT_MAXP,	0x00ff},
	{"itt2ga",	0x000000f0,	0,		SROM4_2G_ITT_MAXP,	0xff00},
	{"itt5ga",	0x000000f0,	0,		SROM4_5G_ITT_MAXP,	0xff00},
	{"pa2gw0a",	0x000000f0,	SRFL_PRHEX,	SROM4_2G_PA,		0xffff},
	{"pa2gw1a",	0x000000f0,	SRFL_PRHEX,	SROM4_2G_PA + 1,	0xffff},
	{"pa2gw2a",	0x000000f0,	SRFL_PRHEX,	SROM4_2G_PA + 2,	0xffff},
	{"pa2gw3a",	0x000000f0,	SRFL_PRHEX,	SROM4_2G_PA + 3,	0xffff},
	{"maxp5ga",	0x000000f0,	0,		SROM4_5G_ITT_MAXP,	0x00ff},
	{"maxp5gha",	0x000000f0,	0,		SROM4_5GLH_MAXP,	0x00ff},
	{"maxp5gla",	0x000000f0,	0,		SROM4_5GLH_MAXP,	0xff00},
	{"pa5gw0a",	0x000000f0,	SRFL_PRHEX,	SROM4_5G_PA,		0xffff},
	{"pa5gw1a",	0x000000f0,	SRFL_PRHEX,	SROM4_5G_PA + 1,	0xffff},
	{"pa5gw2a",	0x000000f0,	SRFL_PRHEX,	SROM4_5G_PA + 2,	0xffff},
	{"pa5gw3a",	0x000000f0,	SRFL_PRHEX,	SROM4_5G_PA + 3,	0xffff},
	{"pa5glw0a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GL_PA,		0xffff},
	{"pa5glw1a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GL_PA + 1,	0xffff},
	{"pa5glw2a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GL_PA + 2,	0xffff},
	{"pa5glw3a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GL_PA + 3,	0xffff},
	{"pa5ghw0a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GH_PA,		0xffff},
	{"pa5ghw1a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GH_PA + 1,	0xffff},
	{"pa5ghw2a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GH_PA + 2,	0xffff},
	{"pa5ghw3a",	0x000000f0,	SRFL_PRHEX,	SROM4_5GH_PA + 3,	0xffff},
	{"maxp2ga",	0x00000700,	0,		SROM8_2G_ITT_MAXP,	0x00ff},
	{"itt2ga",	0x00000700,	0,		SROM8_2G_ITT_MAXP,	0xff00},
	{"itt5ga",	0x00000700,	0,		SROM8_5G_ITT_MAXP,	0xff00},
	{"pa2gw0a",	0x00000700,	SRFL_PRHEX,	SROM8_2G_PA,		0xffff},
	{"pa2gw1a",	0x00000700,	SRFL_PRHEX,	SROM8_2G_PA + 1,	0xffff},
	{"pa2gw2a",	0x00000700,	SRFL_PRHEX,	SROM8_2G_PA + 2,	0xffff},
	{"maxp5ga",	0x00000700,	0,		SROM8_5G_ITT_MAXP,	0x00ff},
	{"maxp5gha",	0x00000700,	0,		SROM8_5GLH_MAXP,	0x00ff},
	{"maxp5gla",	0x00000700,	0,		SROM8_5GLH_MAXP,	0xff00},
	{"pa5gw0a",	0x00000700,	SRFL_PRHEX,	SROM8_5G_PA,		0xffff},
	{"pa5gw1a",	0x00000700,	SRFL_PRHEX,	SROM8_5G_PA + 1,	0xffff},
	{"pa5gw2a",	0x00000700,	SRFL_PRHEX,	SROM8_5G_PA + 2,	0xffff},
	{"pa5glw0a",	0x00000700,	SRFL_PRHEX,	SROM8_5GL_PA,		0xffff},
	{"pa5glw1a",	0x00000700,	SRFL_PRHEX,	SROM8_5GL_PA + 1,	0xffff},
	{"pa5glw2a",	0x00000700,	SRFL_PRHEX,	SROM8_5GL_PA + 2,	0xffff},
	{"pa5ghw0a",	0x00000700,	SRFL_PRHEX,	SROM8_5GH_PA,		0xffff},
	{"pa5ghw1a",	0x00000700,	SRFL_PRHEX,	SROM8_5GH_PA + 1,	0xffff},
	{"pa5ghw2a",	0x00000700,	SRFL_PRHEX,	SROM8_5GH_PA + 2,	0xffff},

	/* sromrev 11 */
	{"maxp2ga",	0xfffff800,	0,			 SROM11_2G_MAXP,	0x00ff},
	{"pa2ga",	0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_2G_PA,		0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_2G_PA + 1,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX,		 SROM11_2G_PA + 2,	0xffff},
	{"rxgains5gmelnagaina",	0xfffff800,	0,		 SROM11_RXGAINS1,	0x0007},
	{"rxgains5gmtrisoa",	0xfffff800,	0,		 SROM11_RXGAINS1,	0x0078},
	{"rxgains5gmtrelnabypa", 0xfffff800,	0,		 SROM11_RXGAINS1,	0x0080},
	{"rxgains5ghelnagaina",	0xfffff800,	0,		 SROM11_RXGAINS1,	0x0700},
	{"rxgains5ghtrisoa",	0xfffff800,	0,		 SROM11_RXGAINS1,	0x7800},
	{"rxgains5ghtrelnabypa", 0xfffff800,	0,		 SROM11_RXGAINS1,	0x8000},
	{"rxgains2gelnagaina",	0xfffff800,	0,		 SROM11_RXGAINS,	0x0007},
	{"rxgains2gtrisoa",	0xfffff800,	0,		 SROM11_RXGAINS,	0x0078},
	{"rxgains2gtrelnabypa",	0xfffff800,	0,		 SROM11_RXGAINS,	0x0080},
	{"rxgains5gelnagaina",	0xfffff800,	0,		 SROM11_RXGAINS,	0x0700},
	{"rxgains5gtrisoa",	0xfffff800,	0,		 SROM11_RXGAINS,	0x7800},
	{"rxgains5gtrelnabypa",	0xfffff800,	0,		 SROM11_RXGAINS,	0x8000},
	{"maxp5ga",	0xfffff800,	SRFL_ARRAY,		 SROM11_5GB1B0_MAXP,	0x00ff},
	{"",		0xfffff800,	SRFL_ARRAY,		 SROM11_5GB1B0_MAXP,	0xff00},
	{"",		0xfffff800,	SRFL_ARRAY,		 SROM11_5GB3B2_MAXP,	0x00ff},
	{"",		0xfffff800,	0,			 SROM11_5GB3B2_MAXP,	0xff00},
	{"pa5ga",	0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB0_PA,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB0_PA + 1,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB0_PA + 2,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB1_PA,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB1_PA + 1,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB1_PA + 2,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB2_PA,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB2_PA + 1,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB2_PA + 2,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB3_PA,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX | SRFL_ARRAY, SROM11_5GB3_PA + 1,	0xffff},
	{"",		0xfffff800,	SRFL_PRHEX,		 SROM11_5GB3_PA + 2,	0xffff},

	{NULL,		0,		0,		0, 			0}
};

#if !(defined(PHY_TYPE_HT) && defined(PHY_TYPE_N) && defined(PHY_TYPE_LP))
#define	PHY_TYPE_HT		7	/* HT-Phy value */
#define	PHY_TYPE_N		4	/* N-Phy value */
#define	PHY_TYPE_LP		5	/* LP-Phy value */
#endif /* !(defined(PHY_TYPE_HT) && defined(PHY_TYPE_N) && defined(PHY_TYPE_LP)) */
#if !defined(PHY_TYPE_AC)
#define	PHY_TYPE_AC		11	/* AC-Phy value */
#endif /* !defined(PHY_TYPE_AC) */
#if !defined(PHY_TYPE_NULL)
#define	PHY_TYPE_NULL		0xf	/* Invalid Phy value */
#endif /* !defined(PHY_TYPE_NULL) */

typedef struct {
	uint16	phy_type;
	uint16	bandrange;
	uint16	chain;
	const char *vars;
} pavars_t;

static const pavars_t pavars[] = {
	/* HTPHY */
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_2G,  0, "pa2gw0a0 pa2gw1a0 pa2gw2a0"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_2G,  1, "pa2gw0a1 pa2gw1a1 pa2gw2a1"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_2G,  2, "pa2gw0a2 pa2gw1a2 pa2gw2a2"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND0, 0, "pa5glw0a0 pa5glw1a0 pa5glw2a0"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND0, 1, "pa5glw0a1 pa5glw1a1 pa5glw2a1"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND0, 2, "pa5glw0a2 pa5glw1a2 pa5glw2a2"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND1, 0, "pa5gw0a0 pa5gw1a0 pa5gw2a0"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND1, 1, "pa5gw0a1 pa5gw1a1 pa5gw2a1"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND1, 2, "pa5gw0a2 pa5gw1a2 pa5gw2a2"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND2, 0, "pa5ghw0a0 pa5ghw1a0 pa5ghw2a0"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND2, 1, "pa5ghw0a1 pa5ghw1a1 pa5ghw2a1"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND2, 2, "pa5ghw0a2 pa5ghw1a2 pa5ghw2a2"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND3, 0, "pa5gw0a3 pa5gw1a3 pa5gw2a3"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND3, 1,  "pa5glw0a3 pa5glw1a3 pa5glw2a3"},
	{PHY_TYPE_HT, WL_CHAN_FREQ_RANGE_5G_BAND3, 2, "pa5ghw0a3 pa5ghw1a3 pa5ghw2a3"},
	/* NPHY */
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_2G,  0, "pa2gw0a0 pa2gw1a0 pa2gw2a0"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_2G,  1, "pa2gw0a1 pa2gw1a1 pa2gw2a1"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5G_BAND0, 0, "pa5glw0a0 pa5glw1a0 pa5glw2a0"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5G_BAND0, 1, "pa5glw0a1 pa5glw1a1 pa5glw2a1"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5G_BAND1, 0, "pa5gw0a0 pa5gw1a0 pa5gw2a0"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5G_BAND1, 1, "pa5gw0a1 pa5gw1a1 pa5gw2a1"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5G_BAND2, 0, "pa5ghw0a0 pa5ghw1a0 pa5ghw2a0"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5G_BAND2, 1, "pa5ghw0a1 pa5ghw1a1 pa5ghw2a1"},
	/* LPPHY */
	{PHY_TYPE_LP, WL_CHAN_FREQ_RANGE_2G,  0, "pa0b0 pa0b1 pa0b2"},
	{PHY_TYPE_LP, WL_CHAN_FREQ_RANGE_5GL, 0, "pa1lob0 pa1lob1 pa1lob2"},
	{PHY_TYPE_LP, WL_CHAN_FREQ_RANGE_5GM, 0, "pa1b0 pa1b1 pa1b2"},
	{PHY_TYPE_LP, WL_CHAN_FREQ_RANGE_5GH, 0, "pa1hib0 pa1hib1 pa1hib2"},
	/* ACPHY */
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  0, "pa2ga0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  1, "pa2ga1"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  2, "pa2ga2"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  0, "pa5ga0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  1, "pa5ga1"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  2, "pa5ga2"},
	{PHY_TYPE_NULL, 0, 0, ""}
};

/* pavars table when paparambwver is 1 */
static const pavars_t pavars_bwver_1[] = {
	/* ACPHY */
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  0, "pa2ga0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  1, "pa2gccka0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  1, "pa2ga2"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  0, "pa5ga0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  1, "pa5gbw40a0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  2, "pa5gbw80a0"},
	{PHY_TYPE_NULL, 0, 0, ""}
};

/* pavars table when paparambwver is 2 */
static const pavars_t pavars_bwver_2[] = {
	/* ACPHY */
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  0, "pa2ga0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_2G,  1, "pa2ga1"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  0, "pa5ga0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  1, "pa5ga1"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  2, "pa5gbw4080a0"},
	{PHY_TYPE_AC, WL_CHAN_FREQ_RANGE_5G_4BAND,  3, "pa5gbw4080a1"},
	{PHY_TYPE_NULL, 0, 0, ""}
};

typedef struct {
	uint16	phy_type;
	uint16	bandrange;
	const char *vars;
} povars_t;

static const povars_t povars[] = {
	/* NPHY */
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_2G,  "mcs2gpo0 mcs2gpo1 mcs2gpo2 mcs2gpo3 "
	"mcs2gpo4 mcs2gpo5 mcs2gpo6 mcs2gpo7"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5GL, "mcs5glpo0 mcs5glpo1 mcs5glpo2 mcs5glpo3 "
	"mcs5glpo4 mcs5glpo5 mcs5glpo6 mcs5glpo7"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5GM, "mcs5gpo0 mcs5gpo1 mcs5gpo2 mcs5gpo3 "
	"mcs5gpo4 mcs5gpo5 mcs5gpo6 mcs5gpo7"},
	{PHY_TYPE_N, WL_CHAN_FREQ_RANGE_5GH, "mcs5ghpo0 mcs5ghpo1 mcs5ghpo2 mcs5ghpo3 "
	"mcs5ghpo4 mcs5ghpo5 mcs5ghpo6 mcs5ghpo7"},
	{PHY_TYPE_NULL, 0, ""}
};

typedef struct {
	uint8	tag;		/* Broadcom subtag name */
	uint32	revmask;	/* Supported cis_sromrev */
	uint8	len;		/* Length field of the tuple, note that it includes the
				 * subtag name (1 byte): 1 + tuple content length
				 */
	const char *params;
} cis_tuple_t;

#define OTP_RAW		(0xff - 1)	/* Reserved tuple number for wrvar Raw input */
#define OTP_VERS_1	(0xff - 2)	/* CISTPL_VERS_1 */
#define OTP_MANFID	(0xff - 3)	/* CISTPL_MANFID */
#define OTP_RAW1	(0xff - 4)	/* Like RAW, but comes first */

static const cis_tuple_t cis_hnbuvars[] = {
	{OTP_RAW1,		0xffffffff, 0, ""},	/* special case */
	{OTP_VERS_1,	0xffffffff, 0, "smanf sproductname"},	/* special case (non BRCM tuple) */
	{OTP_MANFID,	0xffffffff, 4, "2manfid 2prodid"},	/* special case (non BRCM tuple) */
	/* Unified OTP: tupple to embed USB manfid inside SDIO CIS */
	{HNBU_UMANFID,		0xffffffff, 8, "8usbmanfid"},
	{HNBU_SROMREV,		0xffffffff, 2, "1sromrev"},
	/* NOTE: subdevid is also written to boardtype.
	 *       Need to write HNBU_BOARDTYPE to change it if it is different.
	 */
	{HNBU_CHIPID,		0xffffffff, 11, "2vendid 2devid 2chiprev 2subvendid 2subdevid"},
	{HNBU_BOARDREV,		0xffffffff, 3, "2boardrev"},
	{HNBU_PAPARMS,		0xffffffff, 10, "2pa0b0 2pa0b1 2pa0b2 1pa0itssit 1pa0maxpwr 1opo"},
	{HNBU_AA,		0xffffffff, 3, "1aa2g 1aa5g"},
	{HNBU_AA,		0xffffffff, 3, "1aa0 1aa1"}, /* backward compatibility */
	{HNBU_AG,		0xffffffff, 5, "1ag0 1ag1 1ag2 1ag3"},
	{HNBU_BOARDFLAGS,	0xffffffff, 13, "4boardflags 4boardflags2 4boardflags3"},
	{HNBU_LEDS,		0xffffffff, 17, "1ledbh0 1ledbh1 1ledbh2 1ledbh3 1ledbh4 1ledbh5 "
	"1ledbh6 1ledbh7 1ledbh8 1ledbh9 1ledbh10 1ledbh11 1ledbh12 1ledbh13 1ledbh14 1ledbh15"},
	{HNBU_CCODE,		0xffffffff, 4, "2ccode 1cctl"},
	{HNBU_CCKPO,		0xffffffff, 3, "2cckpo"},
	{HNBU_OFDMPO,		0xffffffff, 5, "4ofdmpo"},
	{HNBU_PAPARMS5G,	0xffffffff, 23, "2pa1b0 2pa1b1 2pa1b2 2pa1lob0 2pa1lob1 2pa1lob2 "
	"2pa1hib0 2pa1hib1 2pa1hib2 1pa1itssit "
	"1pa1maxpwr 1pa1lomaxpwr 1pa1himaxpwr"},
	{HNBU_RDLID,		0xffffffff, 3, "2rdlid"},
	{HNBU_RSSISMBXA2G, 0xffffffff, 3, "0rssismf2g 0rssismc2g "
	"0rssisav2g 0bxa2g"}, /* special case */
	{HNBU_RSSISMBXA5G, 0xffffffff, 3, "0rssismf5g 0rssismc5g "
	"0rssisav5g 0bxa5g"}, /* special case */
	{HNBU_XTALFREQ,		0xffffffff, 5, "4xtalfreq"},
	{HNBU_TRI2G,		0xffffffff, 2, "1tri2g"},
	{HNBU_TRI5G,		0xffffffff, 4, "1tri5gl 1tri5g 1tri5gh"},
	{HNBU_RXPO2G,		0xffffffff, 2, "1rxpo2g"},
	{HNBU_RXPO5G,		0xffffffff, 2, "1rxpo5g"},
	{HNBU_BOARDNUM,		0xffffffff, 3, "2boardnum"},
	{HNBU_MACADDR,		0xffffffff, 7, "6macaddr"},	/* special case */
	{HNBU_RDLSN,		0xffffffff, 3, "2rdlsn"},
	{HNBU_BOARDTYPE,	0xffffffff, 3, "2boardtype"},
	{HNBU_LEDDC,		0xffffffff, 3, "2leddc"},
	{HNBU_RDLRNDIS,		0xffffffff, 2, "1rdlndis"},
	{HNBU_CHAINSWITCH,	0xffffffff, 5, "1txchain 1rxchain 2antswitch"},
	{HNBU_REGREV,		0xffffffff, 2, "1regrev"},
	{HNBU_FEM,		0x000007fe, 5, "0antswctl2g 0triso2g 0pdetrange2g 0extpagain2g "
	"0tssipos2g 0antswctl5g 0triso5g 0pdetrange5g 0extpagain5g 0tssipos5g"}, /* special case */
	{HNBU_PAPARMS_C0,	0x000007fe, 31, "1maxp2ga0 1itt2ga0 2pa2gw0a0 2pa2gw1a0 "
	"2pa2gw2a0 1maxp5ga0 1itt5ga0 1maxp5gha0 1maxp5gla0 2pa5gw0a0 2pa5gw1a0 2pa5gw2a0 "
	"2pa5glw0a0 2pa5glw1a0 2pa5glw2a0 2pa5ghw0a0 2pa5ghw1a0 2pa5ghw2a0"},
	{HNBU_PAPARMS_C1,	0x000007fe, 31, "1maxp2ga1 1itt2ga1 2pa2gw0a1 2pa2gw1a1 "
	"2pa2gw2a1 1maxp5ga1 1itt5ga1 1maxp5gha1 1maxp5gla1 2pa5gw0a1 2pa5gw1a1 2pa5gw2a1 "
	"2pa5glw0a1 2pa5glw1a1 2pa5glw2a1 2pa5ghw0a1 2pa5ghw1a1 2pa5ghw2a1"},
	{HNBU_PO_CCKOFDM,	0xffffffff, 19, "2cck2gpo 4ofdm2gpo 4ofdm5gpo 4ofdm5glpo "
	"4ofdm5ghpo"},
	{HNBU_PO_MCS2G,		0xffffffff, 17, "2mcs2gpo0 2mcs2gpo1 2mcs2gpo2 2mcs2gpo3 "
	"2mcs2gpo4 2mcs2gpo5 2mcs2gpo6 2mcs2gpo7"},
	{HNBU_PO_MCS5GM,	0xffffffff, 17, "2mcs5gpo0 2mcs5gpo1 2mcs5gpo2 2mcs5gpo3 "
	"2mcs5gpo4 2mcs5gpo5 2mcs5gpo6 2mcs5gpo7"},
	{HNBU_PO_MCS5GLH,	0xffffffff, 33, "2mcs5glpo0 2mcs5glpo1 2mcs5glpo2 2mcs5glpo3 "
	"2mcs5glpo4 2mcs5glpo5 2mcs5glpo6 2mcs5glpo7 "
	"2mcs5ghpo0 2mcs5ghpo1 2mcs5ghpo2 2mcs5ghpo3 "
	"2mcs5ghpo4 2mcs5ghpo5 2mcs5ghpo6 2mcs5ghpo7"},
	{HNBU_CCKFILTTYPE,	0xffffffff, 2, "1cckdigfilttype"},
	{HNBU_PO_CDD,		0xffffffff, 3, "2cddpo"},
	{HNBU_PO_STBC,		0xffffffff, 3, "2stbcpo"},
	{HNBU_PO_40M,		0xffffffff, 3, "2bw40po"},
	{HNBU_PO_40MDUP,	0xffffffff, 3, "2bwduppo"},
	{HNBU_RDLRWU,		0xffffffff, 2, "1rdlrwu"},
	{HNBU_WPS,		0xffffffff, 3, "1wpsgpio 1wpsled"},
	{HNBU_USBFS,		0xffffffff, 2, "1usbfs"},
	{HNBU_ELNA2G,           0xffffffff, 2, "1elna2g"},
	{HNBU_ELNA5G,           0xffffffff, 2, "1elna5g"},
	{HNBU_CUSTOM1,		0xffffffff, 5, "4customvar1"},
	{OTP_RAW,		0xffffffff, 0, ""},	/* special case */
	{HNBU_OFDMPO5G,		0xffffffff, 13, "4ofdm5gpo 4ofdm5glpo 4ofdm5ghpo"},
	{HNBU_USBEPNUM,		0xffffffff, 3, "2usbepnum"},
	{HNBU_CCKBW202GPO,	0xffffffff, 5, "2cckbw202gpo 2cckbw20ul2gpo"},
	{HNBU_LEGOFDMBW202GPO,	0xffffffff, 9, "4legofdmbw202gpo 4legofdmbw20ul2gpo"},
	{HNBU_LEGOFDMBW205GPO,	0xffffffff, 25, "4legofdmbw205glpo 4legofdmbw20ul5glpo "
	"4legofdmbw205gmpo 4legofdmbw20ul5gmpo 4legofdmbw205ghpo 4legofdmbw20ul5ghpo"},
	{HNBU_MCS2GPO,	0xffffffff, 13,	"4mcsbw202gpo 4mcsbw20ul2gpo 4mcsbw402gpo"},
	{HNBU_MCS5GLPO,	0xffffffff, 13,	"4mcsbw205glpo 4mcsbw20ul5glpo 4mcsbw405glpo"},
	{HNBU_MCS5GMPO,	0xffffffff, 13,	"4mcsbw205gmpo 4mcsbw20ul5gmpo 4mcsbw405gmpo"},
	{HNBU_MCS5GHPO,	0xffffffff, 13,	"4mcsbw205ghpo 4mcsbw20ul5ghpo 4mcsbw405ghpo"},
	{HNBU_MCS32PO,	0xffffffff, 3,	"2mcs32po"},
	{HNBU_LEG40DUPPO, 	0xffffffff, 3,	"2legofdm40duppo"},
	{HNBU_TEMPTHRESH, 	0xffffffff, 7,	"1tempthresh 0temps_period 0temps_hysteresis "
	"1tempoffset 1tempsense_slope 0tempcorrx 0tempsense_option "
	"1phycal_tempdelta"}, /* special case */
	{HNBU_MUXENAB,		0xffffffff, 2,	"1muxenab"},
	{HNBU_FEM_CFG,		0xfffff800, 5,	"0femctrl 0papdcap2g 0tworangetssi2g 0pdgain2g "
	"0epagain2g 0tssiposslope2g 0gainctrlsph 0papdcap5g 0tworangetssi5g 0pdgain5g 0epagain5g "
	"0tssiposslope5g"}, /* special case */
	{HNBU_ACPA_C0,		0xfffff800, 39,	"2subband5gver 2maxp2ga0 2*3pa2ga0 "
	"1*4maxp5ga0 2*12pa5ga0"},
	{HNBU_ACPA_C1,		0xfffff800, 37,	"2maxp2ga1 2*3pa2ga1 1*4maxp5ga1 2*12pa5ga1"},
	{HNBU_ACPA_C2,		0xfffff800, 37,	"2maxp2ga2 2*3pa2ga2 1*4maxp5ga2 2*12pa5ga2"},
	{HNBU_MEAS_PWR,		0xfffff800, 5,	"1measpower 1measpower1 1measpower2 2rawtempsense"},
	{HNBU_PDOFF,		0xfffff800, 13,	"2pdoffset40ma0 2pdoffset40ma1 2pdoffset40ma2 "
	"2pdoffset80ma0 2pdoffset80ma1 2pdoffset80ma2"},
	{HNBU_ACPPR_2GPO,	0xfffff800, 5,	"2dot11agofdmhrbw202gpo 2ofdmlrbw202gpo"},
	{HNBU_ACPPR_5GPO,	0xfffff800, 31,	"4mcsbw805glpo 4mcsbw1605glpo 4mcsbw805gmpo "
	"4mcsbw1605gmpo 4mcsbw805ghpo 4mcsbw1605ghpo 2mcslr5glpo 2mcslr5gmpo 2mcslr5ghpo"},
	{HNBU_ACPPR_SBPO,	0xfffff800, 33,	"2sb20in40hrpo 2sb20in80and160hr5glpo "
	"2sb40and80hr5glpo 2sb20in80and160hr5gmpo 2sb40and80hr5gmpo 2sb20in80and160hr5ghpo "
	"2sb40and80hr5ghpo 2sb20in40lrpo 2sb20in80and160lr5glpo 2sb40and80lr5glpo "
	"2sb20in80and160lr5gmpo 2sb40and80lr5gmpo 2sb20in80and160lr5ghpo 2sb40and80lr5ghpo "
	"2dot11agduphrpo 2dot11agduplrpo"},
	{HNBU_NOISELVL,		0xfffff800, 16, "1noiselvl2ga0 1noiselvl2ga1 1noiselvl2ga2 "
	"1*4noiselvl5ga0 1*4noiselvl5ga1 1*4noiselvl5ga2"},
	{HNBU_RXGAIN_ERR,	0xfffff800, 16, "1rxgainerr2ga0 1rxgainerr2ga1 1rxgainerr2ga2 "
	"1*4rxgainerr5ga0 1*4rxgainerr5ga1 1*4rxgainerr5ga2"},
	{HNBU_AGBGA,		0xfffff800, 7, "1agbg0 1agbg1 1agbg2 1aga0 1aga1 1aga2"},
	{HNBU_UUID, 		0xffffffff, 17,	"16uuid"},
	{HNBU_WOWLGPIO,		0xffffffff, 2,  "1wowl_gpio"},
	{HNBU_ACRXGAINS_C0,	0xfffff800, 5, "0rxgains5gtrelnabypa0 0rxgains5gtrisoa0 "
	"0rxgains5gelnagaina0 0rxgains2gtrelnabypa0 0rxgains2gtrisoa0 0rxgains2gelnagaina0 "
	"0rxgains5ghtrelnabypa0 0rxgains5ghtrisoa0 0rxgains5ghelnagaina0 0rxgains5gmtrelnabypa0 "
	"0rxgains5gmtrisoa0 0rxgains5gmelnagaina0"},	/* special case */
	{HNBU_ACRXGAINS_C1,	0xfffff800, 5, "0rxgains5gtrelnabypa1 0rxgains5gtrisoa1 "
	"0rxgains5gelnagaina1 0rxgains2gtrelnabypa1 0rxgains2gtrisoa1 0rxgains2gelnagaina1 "
	"0rxgains5ghtrelnabypa1 0rxgains5ghtrisoa1 0rxgains5ghelnagaina1 0rxgains5gmtrelnabypa1 "
	"0rxgains5gmtrisoa1 0rxgains5gmelnagaina1"},	/* special case */
	{HNBU_ACRXGAINS_C2,	0xfffff800, 5, "0rxgains5gtrelnabypa2 0rxgains5gtrisoa2 "
	"0rxgains5gelnagaina2 0rxgains2gtrelnabypa2 0rxgains2gtrisoa2 0rxgains2gelnagaina2 "
	"0rxgains5ghtrelnabypa2 0rxgains5ghtrisoa2 0rxgains5ghelnagaina2 0rxgains5gmtrelnabypa2 "
	"0rxgains5gmtrisoa2 0rxgains5gmelnagaina2"},	/* special case */
	{HNBU_TXDUTY, 		0xfffff800, 9,	"2tx_duty_cycle_ofdm_40_5g "
	"2tx_duty_cycle_thresh_40_5g 2tx_duty_cycle_ofdm_80_5g 2tx_duty_cycle_thresh_80_5g"},
	{HNBU_PDOFF_2G,		0xfffff800, 3,	"0pdoffset2g40ma0 0pdoffset2g40ma1 "
	"0pdoffset2g40ma2 0pdoffset2g40mvalid"},
	{HNBU_ACPA_CCK,		0xfffff800, 7,	"2*3pa2gccka0"},
	{HNBU_ACPA_40,		0xfffff800, 25,	"2*12pa5gbw40a0"},
	{HNBU_ACPA_80,		0xfffff800, 25,	"2*12pa5gbw80a0"},
	{HNBU_ACPA_4080,	0xfffff800, 49,	"2*12pa5gbw4080a0 2*12pa5gbw4080a1"},
	{HNBU_SUBBAND5GVER,	0xfffff800, 3,	"2subband5gver"},
	{HNBU_PAPARAMBWVER,	0xfffff800, 2,	"1paparambwver"},
	{0xFF,			0xffffffff, 0, ""}
};

#endif /* _bcmsrom_tbl_h_ */
