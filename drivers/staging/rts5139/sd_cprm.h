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

#ifndef __RTS51X_SD_CPRM_H
#define __RTS51X_SD_CPRM_H

#include "rts51x_chip.h"
#include "sd.h"

#ifdef SUPPORT_CPRM
int ext_sd_execute_no_data(struct rts51x_chip *chip, unsigned int lun,
			   u8 cmd_idx, u8 standby, u8 acmd, u8 rsp_code,
			   u32 arg);
int ext_sd_execute_read_data(struct rts51x_chip *chip, unsigned int lun,
			     u8 cmd_idx, u8 cmd12, u8 standby, u8 acmd,
			     u8 rsp_code, u32 arg, u32 data_len, void *data_buf,
			     unsigned int buf_len, int use_sg);
int ext_sd_execute_write_data(struct rts51x_chip *chip, unsigned int lun,
			      u8 cmd_idx, u8 cmd12, u8 standby, u8 acmd,
			      u8 rsp_code, u32 arg, u32 data_len,
			      void *data_buf, unsigned int buf_len, int use_sg);

int sd_pass_thru_mode(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int sd_execute_no_data(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int sd_execute_read_data(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int sd_execute_write_data(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int sd_get_cmd_rsp(struct scsi_cmnd *srb, struct rts51x_chip *chip);
int sd_hw_rst(struct scsi_cmnd *srb, struct rts51x_chip *chip);
#endif

#endif /* __RTS51X_SD_CPRM_H */
