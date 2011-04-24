#ifndef __OMAP_COMMON_BOARD_DEVICES__
#define __OMAP_COMMON_BOARD_DEVICES__

#if defined(CONFIG_TOUCHSCREEN_ADS7846) || \
	defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
struct ads7846_platform_data;

void omap_ads7846_init(int bus_num, int gpio_pendown, int gpio_debounce,
		       struct ads7846_platform_data *board_pdata);
#else
static inline void omap_ads7846_init(int bus_num,
				     int gpio_pendown, int gpio_debounce,
				     struct ads7846_platform_data *board_data)
{
}
#endif

#endif /* __OMAP_COMMON_BOARD_DEVICES__ */
