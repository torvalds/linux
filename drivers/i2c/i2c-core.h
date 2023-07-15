/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * i2c-core.h - interfaces internal to the I2C framework
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

int i2c_check_7bit_addr_validity_strict(unsigned short addr);
int i2c_dev_irq_from_resources(const struct resource *resources,
			       unsigned int num_resources);

/*
 * We only allow atomic transfers for very late communication, e.g. to access a
 * PMIC when powering down. Atomic transfers are a corner case and not for
 * generic use!
 */
static inline bool i2c_in_atomic_xfer_mode(void)
{
	return system_state > SYSTEM_RUNNING && !preemptible();
}

static inline int __i2c_lock_bus_helper(struct i2c_adapter *adap)
{
	int ret = 0;

	if (i2c_in_atomic_xfer_mode()) {
		WARN(!adap->algo->master_xfer_atomic && !adap->algo->smbus_xfer_atomic,
		     "No atomic I2C transfer handler for '%s'\n", dev_name(&adap->dev));
		ret = i2c_trylock_bus(adap, I2C_LOCK_SEGMENT) ? 0 : -EAGAIN;
	} else {
		i2c_lock_bus(adap, I2C_LOCK_SEGMENT);
	}

	return ret;
}

static inline int __i2c_check_suspended(struct i2c_adapter *adap)
{
	if (test_bit(I2C_ALF_IS_SUSPENDED, &adap->locked_flags)) {
		if (!test_and_set_bit(I2C_ALF_SUSPEND_REPORTED, &adap->locked_flags))
			dev_WARN(&adap->dev, "Transfer while suspended\n");
		return -ESHUTDOWN;
	}

	return 0;
}

#ifdef CONFIG_ACPI
void i2c_acpi_register_devices(struct i2c_adapter *adap);

int i2c_acpi_get_irq(struct i2c_client *client, bool *wake_capable);
#else /* CONFIG_ACPI */
static inline void i2c_acpi_register_devices(struct i2c_adapter *adap) { }

static inline int i2c_acpi_get_irq(struct i2c_client *client, bool *wake_capable)
{
	return 0;
}
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

#if IS_ENABLED(CONFIG_I2C_SMBUS)
int i2c_setup_smbus_alert(struct i2c_adapter *adap);
#else
static inline int i2c_setup_smbus_alert(struct i2c_adapter *adap)
{
	return 0;
}
#endif
