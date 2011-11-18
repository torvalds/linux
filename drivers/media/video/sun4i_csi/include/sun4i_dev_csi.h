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
