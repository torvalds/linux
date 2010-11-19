/*
 * dac.h - sysfs attributes associated with DACs
 */

/* Deprecated */
#define IIO_DEV_ATTR_DAC(_num, _store, _addr)			\
	IIO_DEVICE_ATTR(dac_##_num, S_IWUSR, NULL, _store, _addr)

#define IIO_DEV_ATTR_OUT_RAW(_num, _store, _addr)				\
	IIO_DEVICE_ATTR(out##_num##_raw, S_IWUSR, NULL, _store, _addr)
