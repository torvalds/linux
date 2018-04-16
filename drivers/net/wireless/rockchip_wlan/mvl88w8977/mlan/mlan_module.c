/** @file mlan_module.c
 *
 *  @brief This file declares the exported symbols from MLAN.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/******************************************************
Change log:
    12/08/2008: initial version
******************************************************/

#ifdef LINUX
#include <linux/module.h>
#include "mlan_decl.h"
#include "mlan_ioctl.h"

EXPORT_SYMBOL(mlan_register);
EXPORT_SYMBOL(mlan_unregister);
EXPORT_SYMBOL(mlan_init_fw);
EXPORT_SYMBOL(mlan_set_init_param);
EXPORT_SYMBOL(mlan_dnld_fw);
EXPORT_SYMBOL(mlan_shutdown_fw);
EXPORT_SYMBOL(mlan_send_packet);
EXPORT_SYMBOL(mlan_ioctl);
EXPORT_SYMBOL(mlan_main_process);
EXPORT_SYMBOL(mlan_rx_process);
EXPORT_SYMBOL(mlan_select_wmm_queue);
EXPORT_SYMBOL(mlan_interrupt);
#if defined(SYSKT)
EXPORT_SYMBOL(mlan_hs_callback);
#endif /* SYSKT_MULTI || SYSKT */

EXPORT_SYMBOL(mlan_pm_wakeup_card);
EXPORT_SYMBOL(mlan_is_main_process_running);

MODULE_DESCRIPTION("M-WLAN MLAN Driver");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_VERSION(MLAN_RELEASE_VERSION);
MODULE_LICENSE("GPL");
#endif /* LINUX */
