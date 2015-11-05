#include <linux/kernel.h>
#include <linux/module.h>

static bool dss_initialized;
static const struct dispc_ops *ops;

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

void dispc_set_ops(const struct dispc_ops *o)
{
	ops = o;
}
EXPORT_SYMBOL(dispc_set_ops);

const struct dispc_ops *dispc_get_ops(void)
{
	return ops;
}
EXPORT_SYMBOL(dispc_get_ops);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("OMAP Display Subsystem Base");
MODULE_LICENSE("GPL v2");
