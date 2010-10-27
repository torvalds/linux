/* Copyright (C) 2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/types.h>
#include "pmcc4_sysdep.h"
#include "sbecom_inline_linux.h"
#include "libsbew.h"
#include "pmcc4_private.h"
#include "pmcc4.h"
#include "sbe_bid.h"

#ifdef SBE_INCLUDE_SYMBOLS
#define STATIC
#else
#define STATIC  static
#endif


char       *
sbeid_get_bdname (ci_t * ci)
{
    char       *np = 0;

    switch (ci->brd_id)
    {
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_E1):
        np = "wanPTMC-256T3 <E1>";
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_T1):
        np = "wanPTMC-256T3 <T1>";
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C4T1E1):
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C4T1E1_L):
        np = "wanPMC-C4T1E1";
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1):
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1_L):
        np = "wanPMC-C2T1E1";
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1):
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1_L):
        np = "wanPMC-C1T1E1";
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C4T1E1):
        np = "wanPCI-C4T1E1";
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C2T1E1):
        np = "wanPCI-C2T1E1";
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C1T1E1):
        np = "wanPCI-C1T1E1";
        break;
    default:
        /*** np = "<unknown>";  ***/
        np = "wanPCI-CxT1E1";
        break;
    }

    return np;
}


/* given the presetting of brd_id, set the corresponding hdw_id */

void
sbeid_set_hdwbid (ci_t * ci)
{
    /*
     * set SBE's unique hardware identification (for legacy boards might not
     * have this register implemented)
     */

    switch (ci->brd_id)
    {
        case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_E1):
        ci->hdw_bid = SBE_BID_256T3_E1; /* 0x46 - SBE wanPTMC-256T3 (E1
                                         * Version) */
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_T1):
        ci->hdw_bid = SBE_BID_256T3_T1; /* 0x42 - SBE wanPTMC-256T3 (T1
                                         * Version) */
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C4T1E1):
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C4T1E1_L):
        /*
         * This Board ID is a generic identification.  Use the found number
         * of ports to further define this hardware.
         */
        switch (ci->max_port)
        {
        default:                    /* shouldn't need a default, but have one
                                     * anyway */
        case 4:
            ci->hdw_bid = SBE_BID_PMC_C4T1E1;   /* 0xC4 - SBE wanPMC-C4T1E1 */
            break;
        case 2:
            ci->hdw_bid = SBE_BID_PMC_C2T1E1;   /* 0xC2 - SBE wanPMC-C2T1E1 */
            ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1);
            break;
        case 1:
            ci->hdw_bid = SBE_BID_PMC_C1T1E1;   /* 0xC1 - SBE wanPMC-C1T1E1 */
            ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1);
            break;
        }
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1):
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1_L):
        ci->hdw_bid = SBE_BID_PMC_C2T1E1;       /* 0xC2 - SBE wanPMC-C2T1E1 */
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1):
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1_L):
        ci->hdw_bid = SBE_BID_PMC_C1T1E1;       /* 0xC1 - SBE wanPMC-C1T1E1 */
        break;
#ifdef SBE_PMCC4_ENABLE
        /*
         * This case is entered as a result of the inability to obtain the
         * <bid> from the board's EEPROM.  Assume a PCI board and set
         * <hdsbid> according to the number ofr found ports.
         */
    case 0:
        /* start by assuming 4-port for ZERO casing */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C4T1E1);
        /* drop thru to set hdw_bid and alternate PCI CxT1E1 settings */
#endif
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C4T1E1):
        /*
         * This Board ID is a generic identification.  Use the number of
         * found ports to further define this hardware.
         */
        switch (ci->max_port)
        {
        default:                    /* shouldn't need a default, but have one
                                     * anyway */
        case 4:
            ci->hdw_bid = SBE_BID_PCI_C4T1E1;   /* 0x04 - SBE wanPCI-C4T1E1 */
            break;
        case 2:
            ci->hdw_bid = SBE_BID_PCI_C2T1E1;   /* 0x02 - SBE wanPCI-C2T1E1 */
            ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C2T1E1);
            break;
        case 1:
            ci->hdw_bid = SBE_BID_PCI_C1T1E1;   /* 0x01 - SBE wanPCI-C1T1E1 */
            ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C1T1E1);
            break;
        }
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C2T1E1):
        ci->hdw_bid = SBE_BID_PCI_C2T1E1;       /* 0x02 - SBE wanPCI-C2T1E1 */
        break;
    case SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C1T1E1):
        ci->hdw_bid = SBE_BID_PCI_C1T1E1;       /* 0x01 - SBE wanPCI-C1T1E1 */
        break;
    default:
        /*** bid = "<unknown>";  ***/
        ci->hdw_bid = SBE_BID_PMC_C4T1E1;       /* 0x41 - SBE wanPTMC-C4T1E1 */
        break;
    }
}

/* given the presetting of hdw_bid, set the corresponding brd_id */

void
sbeid_set_bdtype (ci_t * ci)
{
    /* set SBE's unique PCI VENDOR/DEVID */
    switch (ci->hdw_bid)
    {
        case SBE_BID_C1T3:      /* SBE wanPMC-C1T3 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T3);
        break;
    case SBE_BID_C24TE1:            /* SBE wanPTMC-C24TE1 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_C24TE1);
        break;
    case SBE_BID_256T3_E1:          /* SBE wanPTMC-256T3 E1 Version */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_E1);
        break;
    case SBE_BID_256T3_T1:          /* SBE wanPTMC-256T3 T1 Version */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_T1);
        break;
    case SBE_BID_PMC_C4T1E1:        /* 0xC4 - SBE wanPMC-C4T1E1 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C4T1E1);
        break;
    case SBE_BID_PMC_C2T1E1:        /* 0xC2 - SBE wanPMC-C2T1E1 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1);
        break;
    case SBE_BID_PMC_C1T1E1:        /* 0xC1 - SBE wanPMC-C1T1E1 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1);
        break;
    case SBE_BID_PCI_C4T1E1:        /* 0x04 - SBE wanPCI-C4T1E1 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C4T1E1);
        break;
    case SBE_BID_PCI_C2T1E1:        /* 0x02 - SBE wanPCI-C2T1E1 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C2T1E1);
        break;
    case SBE_BID_PCI_C1T1E1:        /* 0x01 - SBE wanPCI-C1T1E1 */
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C1T1E1);
        break;

    default:
        /*** hdw_bid = "<unknown>";  ***/
        ci->brd_id = SBE_BOARD_ID (PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C4T1E1);
        break;
    }
}


/***  End-of-File  ***/
