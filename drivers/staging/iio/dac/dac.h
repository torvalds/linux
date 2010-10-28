/*
 * dac.h - sysfs attributes associated with DACs
 */

#define IIO_DEV_ATTR_DAC(_num, _store, _addr)			\
	IIO_DEVICE_ATTR(dac_##_num, S_IWUSR, NULL, _store, _addr)
