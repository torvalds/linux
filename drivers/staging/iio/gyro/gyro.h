
#include "../sysfs.h"

/* Gyroscope types of attribute */

#define IIO_DEV_ATTR_GYRO_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_X_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_x_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Y_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_y_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Z_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(gyro_z_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_X_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_x_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Y_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_y_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_Z_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_z_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_GYRO_SCALE(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(gyro_scale, S_IRUGO, _show, _store, _addr)

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
