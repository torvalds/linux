// SPDX-License-Identifier: GPL-2.0-only

#include <linux/string.h>

#include <drm/drm_drv.h>
#include <drm/drm_edid.h>

#include "udl_drv.h"
#include "udl_edid.h"

static int udl_read_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct udl_device *udl = data;
	struct drm_device *dev = &udl->drm;
	struct usb_device *udev = udl_to_usb_device(udl);
	u8 *read_buff;
	int idx, ret;
	size_t i;

	read_buff = kmalloc(2, GFP_KERNEL);
	if (!read_buff)
		return -ENOMEM;

	if (!drm_dev_enter(dev, &idx)) {
		ret = -ENODEV;
		goto err_kfree;
	}

	for (i = 0; i < len; i++) {
		int bval = (i + block * EDID_LENGTH) << 8;

		ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				      0x02, (0x80 | (0x02 << 5)), bval,
				      0xA1, read_buff, 2, USB_CTRL_GET_TIMEOUT);
		if (ret < 0) {
			drm_err(dev, "Read EDID byte %zu failed err %x\n", i, ret);
			goto err_drm_dev_exit;
		} else if (ret < 1) {
			ret = -EIO;
			drm_err(dev, "Read EDID byte %zu failed\n", i);
			goto err_drm_dev_exit;
		}

		buf[i] = read_buff[1];
	}

	drm_dev_exit(idx);
	kfree(read_buff);

	return 0;

err_drm_dev_exit:
	drm_dev_exit(idx);
err_kfree:
	kfree(read_buff);
	return ret;
}

bool udl_probe_edid(struct udl_device *udl)
{
	u8 hdr[8];
	int ret;

	ret = udl_read_edid_block(udl, hdr, 0, sizeof(hdr));
	if (ret)
		return false;

	/*
	 * The adapter sends all-zeros if no monitor has been
	 * connected. We consider anything else a connection.
	 */
	return !!memchr_inv(hdr, 0, sizeof(hdr));
}

const struct drm_edid *udl_edid_read(struct drm_connector *connector)
{
	struct udl_device *udl = to_udl(connector->dev);

	return drm_edid_read_custom(connector, udl_read_edid_block, udl);
}
