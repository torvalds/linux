#ifndef _INC_SBEBID_H_
#define _INC_SBEBID_H_

/*-----------------------------------------------------------------------------
 * sbe_bid.h -
 *
 * Copyright (C) 2004-2005  SBE, Inc.
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
 *
 * For further information, contact via email: support@sbei.com
 * SBE, Inc.  San Ramon, California  U.S.A.
 *
 *-----------------------------------------------------------------------------
 */

#define SBE_BID_REG        0x00000000   /* Board ID Register */

#define SBE_BID_256T3_E1         0x46   /* SBE wanPTMC-256T3 (E1 Version) */
#define SBE_BID_256T3_T1         0x42   /* SBE wanPTMC-256T3 (T1 Version) */
#define SBE_BID_2T3E3            0x43   /* SBE wanPMC-2T3E3 */
#define SBE_BID_C1T3             0x45   /* SBE wanPMC-C1T3 */
#define SBE_BID_C24TE1           0x47   /* SBE wanPTMC-C24TE1  */
#define SBE_BID_C24TE1_RTM_24    0x48   /* C24TE1 RTM (24 Port) */
#define SBE_BID_C24TE1_RTM_12    0x49   /* C24TE1 RTM (12 Port) */
#define SBE_BID_C24TE1_RTM_12DSU 0x4A   /* C24TE1 RTM (12 Port/DSU) */
#define SBE_BID_C24TE1_RTM_T3    0x4B   /* C24TE1 RTM (T3) */
#define SBE_BID_C4T1E1           0x41   /* SBE wanPTMC-C4T1E1 */
#define SBE_BID_HC4T1E1          0x44   /* SBE wanADAPT-HC4T1E1 */

/* bogus temporary usage values */
#define SBE_BID_PMC_C4T1E1       0xC4   /* SBE wanPMC-C4T1E1 (4 Port) */
#define SBE_BID_PMC_C2T1E1       0xC2   /* SBE wanPMC-C2T1E1 (2 Port) */
#define SBE_BID_PMC_C1T1E1       0xC1   /* SBE wanPMC-C1T1E1 (1 Port) */
#define SBE_BID_PCI_C4T1E1       0x04   /* SBE wanPCI-C4T1E1 (4 Port) */
#define SBE_BID_PCI_C2T1E1       0x02   /* SBE wanPCI-C2T1E1 (2 Port) */
#define SBE_BID_PCI_C1T1E1       0x01   /* SBE wanPCI-C1T1E1 (1 Port) */

#endif                          /*** _INC_SBEBID_H_ ***/
