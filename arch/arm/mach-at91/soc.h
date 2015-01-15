/*
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

struct at91_init_soc {
	int builtin;
	void (*map_io)(void);
};

extern struct at91_init_soc at91_boot_soc;
extern struct at91_init_soc at91rm9200_soc;
extern struct at91_init_soc sama5d3_soc;
extern struct at91_init_soc sama5d4_soc;

#define AT91_SOC_START(_name)				\
struct at91_init_soc __initdata _name##_soc		\
 __used							\
						= {	\
	.builtin	= 1,				\

#define AT91_SOC_END					\
};

static inline int at91_soc_is_enabled(void)
{
	return at91_boot_soc.builtin;
}

#if !defined(CONFIG_SOC_AT91RM9200)
#define at91rm9200_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_SAMA5D3)
#define sama5d3_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_SAMA5D4)
#define sama5d4_soc	at91_boot_soc
#endif
