/*
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

struct at91_init_soc {
	int builtin;
	u32 extern_irq;
	unsigned int *default_irq_priority;
	void (*map_io)(void);
	void (*ioremap_registers)(void);
	void (*register_clocks)(void);
	void (*register_devices)(void);
	void (*init)(void);
	void (*init_time)(void);
};

extern struct at91_init_soc at91_boot_soc;
extern struct at91_init_soc at91rm9200_soc;
extern struct at91_init_soc at91sam9260_soc;
extern struct at91_init_soc at91sam9261_soc;
extern struct at91_init_soc at91sam9263_soc;
extern struct at91_init_soc at91sam9g45_soc;
extern struct at91_init_soc at91sam9rl_soc;
extern struct at91_init_soc at91sam9x5_soc;
extern struct at91_init_soc at91sam9n12_soc;
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

#if !defined(CONFIG_SOC_AT91SAM9260)
#define at91sam9260_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_AT91SAM9261)
#define at91sam9261_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_AT91SAM9263)
#define at91sam9263_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_AT91SAM9G45)
#define at91sam9g45_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_AT91SAM9RL)
#define at91sam9rl_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_AT91SAM9X5)
#define at91sam9x5_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_AT91SAM9N12)
#define at91sam9n12_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_SAMA5D3)
#define sama5d3_soc	at91_boot_soc
#endif

#if !defined(CONFIG_SOC_SAMA5D4)
#define sama5d4_soc	at91_boot_soc
#endif
