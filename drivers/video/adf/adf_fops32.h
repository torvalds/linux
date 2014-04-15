#ifndef __VIDEO_ADF_ADF_FOPS32_H
#define __VIDEO_ADF_ADF_FOPS32_H

#include <linux/compat.h>
#include <linux/ioctl.h>

#include <video/adf.h>

#define ADF_POST_CONFIG32 \
		_IOW(ADF_IOCTL_TYPE, 2, struct adf_post_config32)
#define ADF_GET_DEVICE_DATA32 \
		_IOR(ADF_IOCTL_TYPE, 4, struct adf_device_data32)
#define ADF_GET_INTERFACE_DATA32 \
		_IOR(ADF_IOCTL_TYPE, 5, struct adf_interface_data32)
#define ADF_GET_OVERLAY_ENGINE_DATA32 \
		_IOR(ADF_IOCTL_TYPE, 6, struct adf_overlay_engine_data32)

struct adf_post_config32 {
	compat_size_t n_interfaces;
	compat_uptr_t interfaces;

	compat_size_t n_bufs;
	compat_uptr_t bufs;

	compat_size_t custom_data_size;
	compat_uptr_t custom_data;

	__s64 complete_fence;
};

struct adf_device_data32 {
	char name[ADF_NAME_LEN];

	compat_size_t n_attachments;
	compat_uptr_t attachments;

	compat_size_t n_allowed_attachments;
	compat_uptr_t allowed_attachments;

	compat_size_t custom_data_size;
	compat_uptr_t custom_data;
};

struct adf_interface_data32 {
	char name[ADF_NAME_LEN];

	__u8 type;
	__u32 id;
	/* e.g. type=ADF_INTF_TYPE_DSI, id=1 => DSI.1 */
	__u32 flags;

	__u8 dpms_state;
	__u8 hotplug_detect;
	__u16 width_mm;
	__u16 height_mm;

	struct drm_mode_modeinfo current_mode;
	compat_size_t n_available_modes;
	compat_uptr_t available_modes;

	compat_size_t custom_data_size;
	compat_uptr_t custom_data;
};

struct adf_overlay_engine_data32 {
	char name[ADF_NAME_LEN];

	compat_size_t n_supported_formats;
	compat_uptr_t supported_formats;

	compat_size_t custom_data_size;
	compat_uptr_t custom_data;
};

long adf_file_compat_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg);

#endif /* __VIDEO_ADF_ADF_FOPS32_H */
