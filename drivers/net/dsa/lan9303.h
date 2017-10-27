#include <linux/regmap.h>
#include <linux/device.h>
#include <net/dsa.h>

struct lan9303;

struct lan9303_phy_ops {
	/* PHY 1 and 2 access*/
	int	(*phy_read)(struct lan9303 *chip, int port, int regnum);
	int	(*phy_write)(struct lan9303 *chip, int port,
			     int regnum, u16 val);
};

struct lan9303 {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_data;
	struct gpio_desc *reset_gpio;
	u32 reset_duration; /* in [ms] */
	bool phy_addr_sel_strap;
	struct dsa_switch *ds;
	struct mutex indirect_mutex; /* protect indexed register access */
	const struct lan9303_phy_ops *ops;
};

extern const struct regmap_access_table lan9303_register_set;
extern const struct lan9303_phy_ops lan9303_indirect_phy_ops;

int lan9303_probe(struct lan9303 *chip, struct device_node *np);
int lan9303_remove(struct lan9303 *chip);
