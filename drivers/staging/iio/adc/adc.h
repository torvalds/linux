/*
 * adc.h - sysfs attributes associated with ADCs
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Copyright (c) 2008 Jonathan Cameron <jic23@cam.ac.uk>
 *
 */

/* Deprecated */
#define IIO_DEV_ATTR_ADC(_num, _show, _addr)			\
  IIO_DEVICE_ATTR(adc_##_num, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_IN_RAW(_num, _show, _addr)				\
	IIO_DEVICE_ATTR(in##_num##_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_IN_NAMED_RAW(_num, _name, _show, _addr)		\
	IIO_DEVICE_ATTR(in##_num##_##_name##_raw, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_IN_DIFF_RAW(_nump, _numn, _show, _addr)		\
	IIO_DEVICE_ATTR_NAMED(in##_nump##min##_numn##_raw,		\
			      in##_nump-in##_numn##_raw,		\
			      S_IRUGO,					\
			      _show,					\
			      NULL,					\
			      _addr)


#define IIO_CONST_ATTR_IN_NAMED_OFFSET(_num, _name, _string)	\
	IIO_CONST_ATTR(in##_num##_##_name##_offset, _string)

#define IIO_CONST_ATTR_IN_NAMED_SCALE(_num, _name, _string)	\
	IIO_CONST_ATTR(in##_num##_##_name##_scale, _string)

#define IIO_EVENT_CODE_IN_HIGH_THRESH(a)				\
	IIO_UNMOD_EVENT_CODE(IIO_EV_CLASS_IN, a, IIO_EV_TYPE_THRESH,	\
			     IIO_EV_DIR_RISING)
#define IIO_EVENT_CODE_IN_LOW_THRESH(a)				     \
	IIO_UNMOD_EVENT_CODE(IIO_EV_CLASS_IN, a, IIO_EV_TYPE_THRESH, \
			     IIO_EV_DIR_FALLING)
