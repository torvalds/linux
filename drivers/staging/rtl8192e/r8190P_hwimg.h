/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef __INC_HAL8190Pci_FW_IMG_H
#define __INC_HAL8190Pci_FW_IMG_H

/*Created on  2008/12/ 3,  3:26*/

#include <linux/types.h>

#define BootArrayLengthPci 344
extern u8 Rtl8190PciFwBootArray[BootArrayLengthPci];
#define MainArrayLengthPci 55388
extern u8 Rtl8190PciFwMainArray[MainArrayLengthPci];
#define DataArrayLengthPci 2960
extern u8 Rtl8190PciFwDataArray[DataArrayLengthPci];
#define PHY_REGArrayLengthPci 280
extern u32 Rtl8190PciPHY_REGArray[PHY_REGArrayLengthPci];
#define PHY_REG_1T2RArrayLengthPci 280
extern u32 Rtl8190PciPHY_REG_1T2RArray[PHY_REG_1T2RArrayLengthPci];
#define RadioA_ArrayLengthPci 246
extern u32 Rtl8190PciRadioA_Array[RadioA_ArrayLengthPci] ;
#define RadioB_ArrayLengthPci 78
extern u32 Rtl8190PciRadioB_Array[RadioB_ArrayLengthPci] ;
#define RadioC_ArrayLengthPci 246
extern u32 Rtl8190PciRadioC_Array[RadioC_ArrayLengthPci] ;
#define RadioD_ArrayLengthPci 78
extern u32 Rtl8190PciRadioD_Array[RadioD_ArrayLengthPci] ;
#define MACPHY_ArrayLengthPci 18
extern u32 Rtl8190PciMACPHY_Array[MACPHY_ArrayLengthPci] ;
#define MACPHY_Array_PGLengthPci 21
extern u32 Rtl8190PciMACPHY_Array_PG[MACPHY_Array_PGLengthPci] ;
#define AGCTAB_ArrayLengthPci 384
extern u32 Rtl8190PciAGCTAB_Array[AGCTAB_ArrayLengthPci] ;

#endif
