/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SPL2SW_DESC_H__
#define __SPL2SW_DESC_H__

void spl2sw_rx_descs_flush(struct spl2sw_common *comm);
void spl2sw_tx_descs_clean(struct spl2sw_common *comm);
void spl2sw_rx_descs_clean(struct spl2sw_common *comm);
void spl2sw_descs_clean(struct spl2sw_common *comm);
void spl2sw_descs_free(struct spl2sw_common *comm);
void spl2sw_tx_descs_init(struct spl2sw_common *comm);
int  spl2sw_rx_descs_init(struct spl2sw_common *comm);
int  spl2sw_descs_alloc(struct spl2sw_common *comm);
int  spl2sw_descs_init(struct spl2sw_common *comm);

#endif
