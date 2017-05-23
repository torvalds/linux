/*
 * i2c-core.h - interfaces internal to the I2C framework
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/rwsem.h>

struct i2c_devinfo {
	struct list_head	list;
	int			busnum;
	struct i2c_board_info	board_info;
};

/* board_lock protects board_list and first_dynamic_bus_num.
 * only i2c core components are allowed to use these symbols.
 */
extern struct rw_semaphore	__i2c_board_lock;
extern struct list_head	__i2c_board_list;
extern int		__i2c_first_dynamic_bus_num;

int i2c_check_addr_validity(unsigned addr, unsigned short flags);
int i2c_check_7bit_addr_validity_strict(unsigned short addr);

#ifdef CONFIG_ACPI
void i2c_acpi_register_devices(struct i2c_adapter *adap);
#else /* CONFIG_ACPI */
static inline void i2c_acpi_register_devices(struct i2c_adapter *adap) { }
#endif /* CONFIG_ACPI */
extern struct notifier_block i2c_acpi_notifier;

#ifdef CONFIG_ACPI_I2C_OPREGION
int i2c_acpi_install_space_handler(struct i2c_adapter *adapter);
void i2c_acpi_remove_space_handler(struct i2c_adapter *adapter);
#else /* CONFIG_ACPI_I2C_OPREGION */
static inline int i2c_acpi_install_space_handler(struct i2c_adapter *adapter) { return 0; }
static inline void i2c_acpi_remove_space_handler(struct i2c_adapter *adapter) { }
#endif /* CONFIG_ACPI_I2C_OPREGION */

#ifdef CONFIG_OF
void of_i2c_register_devices(struct i2c_adapter *adap);
#else
static inline void of_i2c_register_devices(struct i2c_adapter *adap) { }
#endif
extern struct notifier_block i2c_of_notifier;
