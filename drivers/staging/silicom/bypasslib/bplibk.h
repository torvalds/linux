/******************************************************************************/
/*                                                                            */
/* bypass library, Copyright (c) 2004 Silicom, Ltd                            */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/*                                                                            */
/* bplib.h                                                                    */
/*                                                                            */
/******************************************************************************/
#ifndef BYPASS_H
#define BYPASS_H

#include "bp_ioctl.h"
#include "libbp_sd.h"

#define IF_NAME            "eth"
#define SILICOM_VID        0x1374
#define SILICOM_BP_PID_MIN 0x24
#define SILICOM_BP_PID_MAX 0x5f
#define INTEL_PEG4BPII_PID  0x10a0
#define INTEL_PEG4BPFII_PID 0x10a1

#define PEGII_IF_SERIES(vid, pid) \
        ((vid==0x8086)&& \
        ((pid==INTEL_PEG4BPII_PID)||   \
          (pid==INTEL_PEG4BPFII_PID)))

#define EXPORT_SYMBOL_NOVERS EXPORT_SYMBOL

#ifdef BP_VENDOR_SUPPORT
char *bp_desc_array[] =
    { "e1000bp", "e1000bpe", "slcm5700", "bnx2xbp", "ixgbp", "ixgbpe", NULL };
#endif

#endif
