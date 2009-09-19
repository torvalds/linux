#include "../sysfs.h"

/* Light to digital sensor attributes */

#define IIO_DEV_ATTR_LIGHT_INFRARED(_num, _show, _addr)			\
	IIO_DEVICE_ATTR(light_infrared##_num, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LIGHT_BROAD(_num, _show, _addr)			\
	IIO_DEVICE_ATTR(light_broadspectrum##_num, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LIGHT_VISIBLE(_num, _show, _addr)			\
	IIO_DEVICE_ATTR(light_visible##_num, S_IRUGO, _show, NULL, _addr)
