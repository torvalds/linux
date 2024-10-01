/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __SDIO_OSINTF_H__
#define __SDIO_OSINTF_H__



u8 sd_hal_bus_init(struct adapter *padapter);
u8 sd_hal_bus_deinit(struct adapter *padapter);
void sd_c2h_hdl(struct adapter *padapter);

#endif
