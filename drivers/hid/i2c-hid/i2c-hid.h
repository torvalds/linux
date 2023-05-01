/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef I2C_HID_H
#define I2C_HID_H

#include <linux/i2c.h>

#ifdef CONFIG_DMI
struct i2c_hid_desc *i2c_hid_get_dmi_i2c_hid_desc_override(uint8_t *i2c_name);
char *i2c_hid_get_dmi_hid_report_desc_override(uint8_t *i2c_name,
					       unsigned int *size);
u32 i2c_hid_get_dmi_quirks(const u16 vendor, const u16 product);
#else
static inline struct i2c_hid_desc
		   *i2c_hid_get_dmi_i2c_hid_desc_override(uint8_t *i2c_name)
{ return NULL; }
static inline char *i2c_hid_get_dmi_hid_report_desc_override(uint8_t *i2c_name,
							     unsigned int *size)
{ return NULL; }
static inline u32 i2c_hid_get_dmi_quirks(const u16 vendor, const u16 product)
{ return 0; }
#endif

/**
 * struct i2chid_ops - Ops provided to the core.
 *
 * @power_up: do sequencing to power up the device.
 * @power_down: do sequencing to power down the device.
 * @shutdown_tail: called at the end of shutdown.
 */
struct i2chid_ops {
	int (*power_up)(struct i2chid_ops *ops);
	void (*power_down)(struct i2chid_ops *ops);
	void (*shutdown_tail)(struct i2chid_ops *ops);
};

int i2c_hid_core_probe(struct i2c_client *client, struct i2chid_ops *ops,
		       u16 hid_descriptor_address, u32 quirks);
void i2c_hid_core_remove(struct i2c_client *client);

void i2c_hid_core_shutdown(struct i2c_client *client);

extern const struct dev_pm_ops i2c_hid_core_pm;

#endif
