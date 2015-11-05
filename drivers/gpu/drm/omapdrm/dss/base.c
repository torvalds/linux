#include <linux/kernel.h>
#include <linux/module.h>

static bool dss_initialized;

void omapdss_set_is_initialized(bool set)
{
	dss_initialized = set;
}
EXPORT_SYMBOL(omapdss_set_is_initialized);

bool omapdss_is_initialized(void)
{
	return dss_initialized;
}
EXPORT_SYMBOL(omapdss_is_initialized);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("OMAP Display Subsystem Base");
MODULE_LICENSE("GPL v2");
