
#include "nx-842.h"

/* this is needed, separate from the main nx-842.c driver, because that main
 * driver loads the platform drivers during its init(), and it expects one
 * (or none) of the platform drivers to set this pointer to its driver.
 * That means this pointer can't be in the main nx-842 driver, because it
 * wouldn't be accessible until after the main driver loaded, which wouldn't
 * be possible as it's waiting for the platform driver to load.  So place it
 * here.
 */
static struct nx842_driver *driver;
static DEFINE_SPINLOCK(driver_lock);

struct nx842_driver *nx842_platform_driver(void)
{
	return driver;
}
EXPORT_SYMBOL_GPL(nx842_platform_driver);

bool nx842_platform_driver_set(struct nx842_driver *_driver)
{
	bool ret = false;

	spin_lock(&driver_lock);

	if (!driver) {
		driver = _driver;
		ret = true;
	} else
		WARN(1, "can't set platform driver, already set to %s\n",
		     driver->name);

	spin_unlock(&driver_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(nx842_platform_driver_set);

/* only call this from the platform driver exit function */
void nx842_platform_driver_unset(struct nx842_driver *_driver)
{
	spin_lock(&driver_lock);

	if (driver == _driver)
		driver = NULL;
	else if (driver)
		WARN(1, "can't unset platform driver %s, currently set to %s\n",
		     _driver->name, driver->name);
	else
		WARN(1, "can't unset platform driver, already unset\n");

	spin_unlock(&driver_lock);
}
EXPORT_SYMBOL_GPL(nx842_platform_driver_unset);

bool nx842_platform_driver_get(void)
{
	bool ret = false;

	spin_lock(&driver_lock);

	if (driver)
		ret = try_module_get(driver->owner);

	spin_unlock(&driver_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(nx842_platform_driver_get);

void nx842_platform_driver_put(void)
{
	spin_lock(&driver_lock);

	if (driver)
		module_put(driver->owner);

	spin_unlock(&driver_lock);
}
EXPORT_SYMBOL_GPL(nx842_platform_driver_put);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
MODULE_DESCRIPTION("842 H/W Compression platform driver");
