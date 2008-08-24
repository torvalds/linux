void __init eseries_fixup(struct machine_desc *desc,
	struct tag *tags, char **cmdline, struct meminfo *mi);

extern struct pxa2xx_udc_mach_info e7xx_udc_mach_info;
extern struct pxaficp_platform_data e7xx_ficp_platform_data;
extern int e7xx_irda_init(void);

