struct usb_phy_roothub;

struct usb_phy_roothub *usb_phy_roothub_init(struct device *dev);
int usb_phy_roothub_exit(struct usb_phy_roothub *phy_roothub);

int usb_phy_roothub_power_on(struct usb_phy_roothub *phy_roothub);
void usb_phy_roothub_power_off(struct usb_phy_roothub *phy_roothub);
