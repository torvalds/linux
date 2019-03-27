/*
 * Wi-Fi Protected Setup - device attributes
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPS_DEV_ATTR_H
#define WPS_DEV_ATTR_H

struct wps_parse_attr;

int wps_build_manufacturer(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_model_name(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_model_number(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_serial_number(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_dev_name(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_device_attrs(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_os_version(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_vendor_ext_m1(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_rf_bands(struct wps_device_data *dev, struct wpabuf *msg,
		       u8 rf_band);
int wps_build_primary_dev_type(struct wps_device_data *dev,
			       struct wpabuf *msg);
int wps_build_secondary_dev_type(struct wps_device_data *dev,
				 struct wpabuf *msg);
int wps_build_dev_name(struct wps_device_data *dev, struct wpabuf *msg);
int wps_process_device_attrs(struct wps_device_data *dev,
			     struct wps_parse_attr *attr);
int wps_process_os_version(struct wps_device_data *dev, const u8 *ver);
int wps_process_rf_bands(struct wps_device_data *dev, const u8 *bands);
void wps_device_data_free(struct wps_device_data *dev);
int wps_build_vendor_ext(struct wps_device_data *dev, struct wpabuf *msg);
int wps_build_req_dev_type(struct wps_device_data *dev, struct wpabuf *msg,
			   unsigned int num_req_dev_types,
			   const u8 *req_dev_types);

#endif /* WPS_DEV_ATTR_H */
