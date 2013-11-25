#ifndef _PHY_GENERIC_H_
#define _PHY_GENERIC_H_

struct usb_phy_gen_xceiv {
	struct usb_phy phy;
	struct device *dev;
	struct clk *clk;
	struct regulator *vcc;
	int gpio_reset;
	bool reset_active_low;
};

int usb_gen_phy_init(struct usb_phy *phy);
void usb_gen_phy_shutdown(struct usb_phy *phy);

int usb_phy_gen_create_phy(struct device *dev, struct usb_phy_gen_xceiv *nop,
		enum usb_phy_type type, u32 clk_rate, bool needs_vcc);

#endif
