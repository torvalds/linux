#ifndef _AM335x_PHY_CONTROL_H_
#define _AM335x_PHY_CONTROL_H_

struct phy_control {
	void (*phy_power)(struct phy_control *phy_ctrl, u32 id,
			enum usb_dr_mode dr_mode, bool on);
	void (*phy_wkup)(struct phy_control *phy_ctrl, u32 id, bool on);
};

static inline void phy_ctrl_power(struct phy_control *phy_ctrl, u32 id,
				enum usb_dr_mode dr_mode, bool on)
{
	phy_ctrl->phy_power(phy_ctrl, id, dr_mode, on);
}

static inline void phy_ctrl_wkup(struct phy_control *phy_ctrl, u32 id, bool on)
{
	phy_ctrl->phy_wkup(phy_ctrl, id, on);
}

struct phy_control *am335x_get_phy_control(struct device *dev);

#endif
