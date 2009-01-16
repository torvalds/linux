#ifndef _INCLUDE_PALMASOC_H_
#define _INCLUDE_PALMASOC_H_
struct palm27x_asoc_info {
	int	jack_gpio;
};

#ifdef CONFIG_SND_PXA2XX_SOC_PALM27X
void __init palm27x_asoc_set_pdata(struct palm27x_asoc_info *data);
#else
static inline void palm27x_asoc_set_pdata(struct palm27x_asoc_info *data) {}
#endif

#endif
