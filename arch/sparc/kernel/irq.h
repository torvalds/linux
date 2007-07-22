#include <asm/btfixup.h>

BTFIXUPDEF_CALL(void, disable_pil_irq, unsigned int)
BTFIXUPDEF_CALL(void, enable_pil_irq, unsigned int)
BTFIXUPDEF_CALL(void, clear_clock_irq, void)
BTFIXUPDEF_CALL(void, clear_profile_irq, int)
BTFIXUPDEF_CALL(void, load_profile_irq, int, unsigned int)

static inline void disable_pil_irq(unsigned int irq)
{
	BTFIXUP_CALL(disable_pil_irq)(irq);
}

static inline void enable_pil_irq(unsigned int irq)
{
	BTFIXUP_CALL(enable_pil_irq)(irq);
}

static inline void clear_clock_irq(void)
{
	BTFIXUP_CALL(clear_clock_irq)();
}

static inline void clear_profile_irq(int irq)
{
	BTFIXUP_CALL(clear_profile_irq)(irq);
}

static inline void load_profile_irq(int cpu, int limit)
{
	BTFIXUP_CALL(load_profile_irq)(cpu, limit);
}

extern void (*sparc_init_timers)(irq_handler_t lvl10_irq);

extern void claim_ticker14(irq_handler_t irq_handler,
			   int irq,
			   unsigned int timeout);

#ifdef CONFIG_SMP
BTFIXUPDEF_CALL(void, set_cpu_int, int, int)
BTFIXUPDEF_CALL(void, clear_cpu_int, int, int)
BTFIXUPDEF_CALL(void, set_irq_udt, int)

#define set_cpu_int(cpu,level) BTFIXUP_CALL(set_cpu_int)(cpu,level)
#define clear_cpu_int(cpu,level) BTFIXUP_CALL(clear_cpu_int)(cpu,level)
#define set_irq_udt(cpu) BTFIXUP_CALL(set_irq_udt)(cpu)
#endif
