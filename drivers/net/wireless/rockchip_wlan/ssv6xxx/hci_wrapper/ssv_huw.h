/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SSV_HUW_H_
#define _SSV_HUW_H_ 
#include <linux/ioctl.h>
struct ssv_huw_cmd {
    __u32 in_data_len;
    u8* in_data;
#ifndef __x86_64
    __u32 padding1;
#endif
    __u32 out_data_len;
    u8* out_data;
#ifndef __x86_64
        __u32 padding2;
#endif
    __u32 response;
}__attribute__((packed));
#define FILE_DEVICE_SSVSDIO MMC_BLOCK_MAJOR
#define FILE_DEVICE_SSVSDIO_SEQ 0x50
#define FILE_DEVICE_SSVSDIO_NAME "ssvhuwdev"
#if 0
#define IOCTL_SSVSDIO_GET_DRIVER_VERSION \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x01, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_FUNCTION_NUMBER \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x02, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_FUNCTION_FOCUS \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x03, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_FUNCTION_FOCUS \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x04, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_BUS_WIDTH \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x05, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_BUS_WIDTH \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x06, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_BUS_CLOCK \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x07, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_BUS_CLOCK \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x08, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_BLOCK_MODE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x09, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_BLOCK_MODE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x0a, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_BLOCKLEN \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x0b, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_BLOCKLEN \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x0c, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_FN0_BLOCKLEN \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x0d, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_FN0_BLOCKLEN \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x0e, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_BUS_INTERFACE_CONTROL \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x0f, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_BUS_INTERFACE_CONTROL \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x10, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_INT_ENABLE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x11, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_INT_ENABLE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x12, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_AUTO_ACK_INT \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x13, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_AUTO_ACK_INT \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x14, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_ACK_INT \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x15, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_READ_BYTE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x16, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_WRITE_BYTE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x17, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_MULTI_BYTE_IO_PORT \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x18, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_MULTI_BYTE_IO_PORT \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x19, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_READ_MULTI_BYTE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x1a, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_WRITE_MULTI_BYTE \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x1b, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_GET_MULTI_BYTE_REG_IO_PORT \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x1c, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_SET_MULTI_BYTE_REG_IO_PORT \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x1d, struct ssv_huw_cmd)
#endif
#define IOCTL_SSVSDIO_READ_REG \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x1e, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_WRITE_REG \
 _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x1f, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_READ_DATA \
    _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x20, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_WRITE_SRAM \
    _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x21, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_START \
    _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x22, struct ssv_huw_cmd)
#define IOCTL_SSVSDIO_STOP \
    _IOWR( FILE_DEVICE_SSVSDIO, FILE_DEVICE_SSVSDIO_SEQ+0x23, struct ssv_huw_cmd)
#endif
