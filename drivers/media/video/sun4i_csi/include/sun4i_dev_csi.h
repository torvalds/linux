/*
 * drivers/media/video/sun4i_csi/include/sun4i_dev_csi.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __DEV_CSI_H__
#define __DEV_CSI_H__


/*
 * ioctl to proccess sub device
 */
typedef enum tag_CSI_SUBDEV_CMD
{
	CSI_SUBDEV_CMD_GET_INFO = 0x01,
	CSI_SUBDEV_CMD_SET_INFO = 0x02,
}__csi_subdev_cmd_t;

/*
 * control id
 */

typedef enum tag_CSI_SUBDEV_CTL_ID
{
	CSI_SUBDEV_INIT_FULL = 0x01,
	CSI_SUBDEV_INIT_SIMP = 0x02,
	CSI_SUBDEV_RST_ON = 0x03,
	CSI_SUBDEV_RST_OFF = 0x04,
	CSI_SUBDEV_RST_PUL = 0x05,
	CSI_SUBDEV_STBY_ON = 0x06,
	CSI_SUBDEV_STBY_OFF = 0x07,
	CSI_SUBDEV_PWR_ON = 0x08,
	CSI_SUBDEV_PWR_OFF = 0x09,
}__csi_subdev_ctl_id_t;
#endif
