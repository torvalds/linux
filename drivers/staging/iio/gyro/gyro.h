
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
	IIO_DEVICE_ATTR(gyro, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_GYRO_X(_show, _addr)			\
	IIO_DEVICE_ATTR(gyro_x, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_GYRO_Y(_show, _addr)			\
	IIO_DEVICE_ATTR(gyro_y, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_GYRO_Z(_show, _addr)			\
	IIO_DEVICE_ATTR(gyro_z, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_TEMP_X(_show, _addr)			\
	IIO_DEVICE_ATTR(temp_x, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_TEMP_Y(_show, _addr)			\
	IIO_DEVICE_ATTR(temp_y, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_TEMP_Z(_show, _addr)			\
	IIO_DEVICE_ATTR(temp_z, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_INCLI_X(_show, _addr)			\
	IIO_DEVICE_ATTR(incli_x, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_INCLI_Y(_show, _addr)			\
	IIO_DEVICE_ATTR(incli_y, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_INCLI_Z(_show, _addr)			\
	IIO_DEVICE_ATTR(incli_z, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_INCLI_X_OFFSET(_mode, _show, _store, _addr) \
	IIO_DEVICE_ATTR(incli_x_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_INCLI_Y_OFFSET(_mode, _show, _store, _addr) \
	IIO_DEVICE_ATTR(incli_y_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_INCLI_Z_OFFSET(_mode, _show, _store, _addr) \
	IIO_DEVICE_ATTR(incli_z_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ROT(_show, _addr)                    \
	IIO_DEVICE_ATTR(rot, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ROT_OFFSET(_mode, _show, _store, _addr) \
	IIO_DEVICE_ATTR(rot_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ANGL(_show, _addr)                         \
	IIO_DEVICE_ATTR(angl, S_IRUGO, _show, NULL, _addr)
