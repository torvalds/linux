/*
 * Special local versatile callbacks
 */
#include <linux/of.h>
#include <linux/amba/bus.h>
#include <linux/platform_data/video-clcd-versatile.h>

#if defined(CONFIG_PLAT_VERSATILE_CLCD) && defined(CONFIG_OF)
int versatile_clcd_init_panel(struct clcd_fb *fb, struct device_node *panel);
#else
static inline int versatile_clcd_init_panel(struct clcd_fb *fb,
					    struct device_node *panel)
{
	return 0;
}
#endif
