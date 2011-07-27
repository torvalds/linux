
#include "../sysfs.h"

/* Gyroscope types of attribute */

#define IIO_CONST_ATTR_GYRO_OFFSET(_string)	\
	IIO_CONST_ATTR(gyro_offset, _string)

#define IIO_DEV_ATTR_GYRO_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_X_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_x_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Y_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_y_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Z_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_z_offset, _mode, _show, _store, _addr)

#define IIO_CONST_ATTR_GYRO_SCALE(_string)		\
	IIO_CONST_ATTR(gyro_scale, _string)

#define IIO_DEV_ATTR_GYRO_SCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_scale, S_IRUGO, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_X_SCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_x_scale, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Y_SCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_y_scale, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Z_SCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_z_scale, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_CALIBBIAS(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_calibbias, S_IRUGO, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_X_CALIBBIAS(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_x_calibbias, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Y_CALIBBIAS(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_y_calibbias, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Z_CALIBBIAS(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_z_calibbias, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_CALIBSCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_calibscale, S_IRUGO, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_X_CALIBSCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_x_calibscale, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Y_CALIBSCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_y_calibscale, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Z_CALIBSCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_z_calibscale, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Z_QUADRATURE_CORRECTION(_show, _addr)		\
	IIO_DEVICE_ATTR(gyro_z_quadrature_correction_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_GYRO(_show, _addr)			\
	IIO_DEVICE_ATTR(gyro_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_GYRO_X(_show, _addr)			\
	IIO_DEVICE_ATTR(gyro_x_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_GYRO_Y(_show, _addr)			\
	IIO_DEVICE_ATTR(gyro_y_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_GYRO_Z(_show, _addr)			\
	IIO_DEVICE_ATTR(gyro_z_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ANGL(_show, _addr)                         \
	IIO_DEVICE_ATTR(angl_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ANGL_X(_show, _addr)				\
	IIO_DEVICE_ATTR(angl_x_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ANGL_Y(_show, _addr)				\
	IIO_DEVICE_ATTR(angl_y_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ANGL_Z(_show, _addr)				\
	IIO_DEVICE_ATTR(angl_z_raw, S_IRUGO, _show, NULL, _addr)
