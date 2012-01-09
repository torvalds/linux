/* Driver for Realtek RTS51xx USB card reader
 * Header file
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTS51X_MS_MG_H
#define __RTS51X_MS_MG_H

#include "rts51x_chip.h"
#include "ms.h"

int mg_set_leaf_id(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int mg_get_local_EKB(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int mg_chg(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int mg_get_rsp_chg(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int mg_rsp(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int mg_get_ICV(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int mg_set_ICV(struct scsi_cmnd *srb, struct rts51x_chip *chip);

#endif /* __RTS51X_MS_MG_H */
