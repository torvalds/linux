/* Driver for Realtek PCI-Express card reader
 * Header file
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
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
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#ifndef __REALTEK_RTSX_TRANSPORT_H
#define __REALTEK_RTSX_TRANSPORT_H

#include "rtsx.h"
#include "rtsx_chip.h"

#define WAIT_TIME	2000

unsigned int rtsx_stor_access_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb, unsigned int *index,
	unsigned int *offset, enum xfer_buf_dir dir);
void rtsx_stor_set_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb);
void rtsx_stor_get_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb);
void rtsx_invoke_transport(struct scsi_cmnd *srb, struct rtsx_chip *chip);


#define rtsx_init_cmd(chip)			((chip)->ci = 0)

void rtsx_add_cmd(struct rtsx_chip *chip,
		u8 cmd_type, u16 reg_addr, u8 mask, u8 data);
void rtsx_send_cmd_no_wait(struct rtsx_chip *chip);
int rtsx_send_cmd(struct rtsx_chip *chip, u8 card, int timeout);

static inline u8 *rtsx_get_cmd_data(struct rtsx_chip *chip)
{
#ifdef CMD_USING_SG
	return (u8 *)(chip->host_sg_tbl_ptr);
#else
	return (u8 *)(chip->host_cmds_ptr);
#endif
}

int rtsx_transfer_data(struct rtsx_chip *chip, u8 card, void *buf, size_t len,
		int use_sg, enum dma_data_direction dma_dir, int timeout);

int rtsx_transfer_data_partial(struct rtsx_chip *chip, u8 card,
			void *buf, size_t len,
			int use_sg, unsigned int *index, unsigned int *offset,
			enum dma_data_direction dma_dir, int timeout);

#endif   /* __REALTEK_RTSX_TRANSPORT_H */
