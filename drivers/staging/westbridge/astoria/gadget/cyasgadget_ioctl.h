/* cyasgadget_ioctl.h - Linux USB Gadget driver ioctl file for
 * Cypress West Bridge
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef CYASGADGET_IOCTL_H
#define CYASGADGET_IOCTL_H


#include <linux/types.h>
#include <linux/ioctl.h>

typedef struct cy_as_gadget_ioctl_send_object {
	uint32_t status;
	uint32_t byte_count;
	uint32_t transaction_id;
} cy_as_gadget_ioctl_send_object;

typedef struct cy_as_gadget_ioctl_get_object {
	uint32_t status;
	uint32_t byte_count;
} cy_as_gadget_ioctl_get_object;


typedef struct cy_as_gadget_ioctl_tmtp_status {
	cy_bool tmtp_send_complete;
	cy_bool tmtp_get_complete;
	cy_bool tmtp_need_new_blk_tbl;
	cy_as_gadget_ioctl_send_object tmtp_send_complete_data;
	cy_as_gadget_ioctl_get_object tmtp_get_complete_data;
	uint32_t t_usec;
} cy_as_gadget_ioctl_tmtp_status;

/*Init send object data*/
typedef struct cy_as_gadget_ioctl_i_s_o_j_d {
	uint32_t	*blk_addr_p;	   /* starting sector */
	uint16_t	*blk_count_p;	  /* num of sectors in the block */
	/* number of entries in the blk table */
	uint32_t	item_count;
	uint32_t	num_bytes;
	/*  in case if more prcise timestamping is done in kernel mode  */
	uint32_t	t_usec;
	uint32_t	ret_val;
	char	*file_name;
	uint32_t	name_length;

} cy_as_gadget_ioctl_i_s_o_j_d;


/*Init get object data*/
typedef struct cy_as_gadget_ioctl_i_g_o_j_d  {
	uint32_t *blk_addr_p;
	uint16_t *blk_count_p;
	uint32_t item_count;
	uint32_t num_bytes;
	uint32_t tid;
	uint32_t ret_val;
	char *file_name;
	uint32_t name_length;

} cy_as_gadget_ioctl_i_g_o_j_d;

typedef struct cy_as_gadget_ioctl_cancel {
	uint32_t ret_val;
} cy_as_gadget_ioctl_cancel;

#define CYASGADGET_IOC_MAGIC 0xEF
#define CYASGADGET_GETMTPSTATUS \
	_IOW(CYASGADGET_IOC_MAGIC,  0, cy_as_gadget_ioctl_tmtp_status)
#define CYASGADGET_CLEARTMTPSTATUS \
	_IO(CYASGADGET_IOC_MAGIC,   1)
#define CYASGADGET_INITSOJ \
	_IOW(CYASGADGET_IOC_MAGIC,  2, cy_as_gadget_ioctl_i_s_o_j_d)
#define CYASGADGET_INITGOJ \
	_IOW(CYASGADGET_IOC_MAGIC,  3, cy_as_gadget_ioctl_i_g_o_j_d)
#define CYASGADGET_CANCELSOJ \
	_IOW(CYASGADGET_IOC_MAGIC,   4, cy_as_gadget_ioctl_cancel)
#define CYASGADGET_CANCELGOJ \
	_IOW(CYASGADGET_IOC_MAGIC,   5, cy_as_gadget_ioctl_cancel)
#define CYASGADGET_IOC_MAXNR 6

#endif
