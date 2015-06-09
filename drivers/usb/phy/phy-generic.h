#ifndef _PHY_GENERIC_H_
#define _PHY_GENERIC_H_

#include <linux/usb/usb_phy_generic.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

struct usb_phy_generic {
	struct usb_phy phy;
	struct device *dev;
	struct clk *clk;
	struct regulator *vcc;
	struct gpio_desc *gpiod_reset;
	struct gpio_desc *gpiod_vbus;
	struct regulator *vbus_draw;
	bool vbus_draw_enabled;
	unsigned long mA;
	unsigned int vbus;
};

int usb_gen_phy_init(struct usb_phy *phy);
void usb_gen_phy_shutdown(struct usb_phy *phy);

int usb_phy_gen_create_phy(struct device *dev, struct usb_phy_generic *nop,
		struct usb_phy_generic_platform_data *pdata);

#endif
