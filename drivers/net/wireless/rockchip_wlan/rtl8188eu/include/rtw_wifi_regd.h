/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 *****************************************************************************/

#ifndef __RTW_WIFI_REGD_H__
#define __RTW_WIFI_REGD_H__

struct country_code_to_enum_rd {
	u16 countrycode;
	const char *iso_name;
};

enum country_code_type_t {
	COUNTRY_CODE_USER = 0,

	/*add new channel plan above this line */
	COUNTRY_CODE_MAX
}; 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
int rtw_regd_init(_adapter *padapter,
	void (*reg_notifier)(struct wiphy *wiphy,
		struct regulatory_request *request));
void rtw_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request);
#else
int rtw_regd_init(_adapter *padapter,
	int (*reg_notifier)(struct wiphy *wiphy,
		struct regulatory_request *request));
int rtw_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request);
#endif

#endif
