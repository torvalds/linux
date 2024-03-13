/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 */

#ifndef _MEDUSA_DEF_H_
#define _MEDUSA_DEF_H_

/* Video decoder that we supported */
#define VDEC_A		0
#define VDEC_B		1
#define VDEC_C		2
#define VDEC_D		3
#define VDEC_E		4
#define VDEC_F		5
#define VDEC_G		6
#define VDEC_H		7

/* end of display sequence */
#define END_OF_SEQ	0xF;

/* registry string size */
#define MAX_REGISTRY_SZ	40;

#endif
