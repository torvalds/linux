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

#ifndef __RTS51X_TRANSPORT_H
#define __RTS51X_TRANSPORT_H

#include <linux/kernel.h>

#include "rts51x.h"
#include "rts51x_chip.h"

#if 1 /* LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 34) */
#define URB_NO_SETUP_DMA_MAP		0
#endif

unsigned int rts51x_access_sglist(unsigned char *buffer,
				  unsigned int buflen, void *sglist,
				  void **sgptr, unsigned int *offset,
				  enum xfer_buf_dir dir);
unsigned int rts51x_access_xfer_buf(unsigned char *buffer, unsigned int buflen,
				    struct scsi_cmnd *srb,
				    struct scatterlist **sgptr,
				    unsigned int *offset,
				    enum xfer_buf_dir dir);
void rts51x_set_xfer_buf(unsigned char *buffer, unsigned int buflen,
			 struct scsi_cmnd *srb);
void rts51x_get_xfer_buf(unsigned char *buffer, unsigned int buflen,
			 struct scsi_cmnd *srb);

int rts51x_ctrl_transfer(struct rts51x_chip *chip, unsigned int pipe,
			 u8 request, u8 requesttype, u16 value, u16 index,
			 void *data, u16 size, int timeout);
int rts51x_clear_halt(struct rts51x_chip *chip, unsigned int pipe);
int rts51x_transfer_data(struct rts51x_chip *chip, unsigned int pipe,
			 void *buf, unsigned int len, int use_sg,
			 unsigned int *act_len, int timeout);
int rts51x_transfer_data_partial(struct rts51x_chip *chip, unsigned int pipe,
				 void *buf, void **ptr, unsigned int *offset,
				 unsigned int len, int use_sg,
				 unsigned int *act_len, int timeout);

/* whichPipe:
 * 0: bulk in pipe
 * 1: bulk out pipe
 * 2: intr  in pipe */
int rts51x_reset_pipe(struct rts51x_chip *chip, char pipe);

#ifndef POLLING_IN_THREAD
int rts51x_start_epc_transfer(struct rts51x_chip *chip);
void rts51x_cancel_epc_transfer(struct rts51x_chip *chip);
#endif

int rts51x_get_epc_status(struct rts51x_chip *chip, u16 * status);
void rts51x_invoke_transport(struct scsi_cmnd *srb, struct rts51x_chip *chip);

#endif /* __RTS51X_TRANSPORT_H */
