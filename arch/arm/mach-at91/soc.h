/*
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

struct at91_init_soc {
	unsigned int *default_irq_priority;
	void (*map_io)(void);
	void (*register_clocks)(void);
	void (*init)(void);
};

extern struct at91_init_soc at91_boot_soc;
extern struct at91_init_soc at91cap9_soc;
extern struct at91_init_soc at91rm9200_soc;
extern struct at91_init_soc at91sam9260_soc;
extern struct at91_init_soc at91sam9261_soc;
extern struct at91_init_soc at91sam9263_soc;
extern struct at91_init_soc at91sam9g45_soc;
extern struct at91_init_soc at91sam9rl_soc;
extern struct at91_init_soc at91sam9x5_soc;

static inline int at91_soc_is_enabled(void)
{
	return at91_boot_soc.init != NULL;
}

#if !defined(CONFIG_ARCH_AT91CAP9)
#define at91cap9_soc	at91_boot_soc
#endif

#if !defined(CONFIG_ARCH_AT91RM9200)
#define at91rm9200_soc	at91_boot_soc
#endif

#if !(defined(CONFIG_ARCH_AT91SAM9260) || defined(CONFIG_ARCH_AT91SAM9G20))
#define at91sam9260_soc	at91_boot_soc
#endif

#if !(defined(CONFIG_ARCH_AT91SAM9261) || defined(CONFIG_ARCH_AT91SAM9G10))
#define at91sam9261_soc	at91_boot_soc
#endif

#if !defined(CONFIG_ARCH_AT91SAM9263)
#define at91sam9263_soc	at91_boot_soc
#endif

#if !defined(CONFIG_ARCH_AT91SAM9G45)
#define at91sam9g45_soc	at91_boot_soc
#endif

#if !defined(CONFIG_ARCH_AT91SAM9RL)
#define at91sam9rl_soc	at91_boot_soc
#endif

#if !defined(CONFIG_ARCH_AT91SAM9X5)
#define at91sam9x5_soc	at91_boot_soc
#endif
