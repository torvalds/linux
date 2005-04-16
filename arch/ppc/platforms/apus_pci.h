/*
 * Phase5 CybervisionPPC (TVP4020) definitions for the Permedia2 framebuffer
 * driver.
 *
 * Copyright (c) 1998-1999 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 * --------------------------------------------------------------------------
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

#ifndef APUS_PCI_H
#define APUS_PCI_H


#define CSPPC_PCI_BRIDGE		0xfffe0000
#define CSPPC_BRIDGE_ENDIAN		0x0000
#define CSPPC_BRIDGE_INT		0x0010

#define	CVPPC_PCI_CONFIG		0xfffc0000
#define CVPPC_ROM_ADDRESS		0xe2000001
#define CVPPC_REGS_REGION		0xef000000
#define CVPPC_FB_APERTURE_ONE		0xe0000000
#define CVPPC_FB_APERTURE_TWO		0xe1000000
#define CVPPC_FB_SIZE			0x00800000

/* CVPPC_BRIDGE_ENDIAN */
#define CSPPCF_BRIDGE_BIG_ENDIAN	0x02

/* CVPPC_BRIDGE_INT */
#define CSPPCF_BRIDGE_ACTIVE_INT2	0x01


#endif	/* APUS_PCI_H */
