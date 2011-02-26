#include <linux/platform_device.h>

#include <asm/btfixup.h>

/*
 * Platform specific irq configuration
 * The individual platforms assign their platform
 * specifics in their init functions.
 */
struct sparc_irq_config {
	void (*init_timers)(irq_handler_t);
	unsigned int (*build_device_irq)(struct platform_device *op,
	                                 unsigned int real_irq);
};
extern struct sparc_irq_config sparc_irq_config;


/* Dave Redman (djhr@tadpole.co.uk)
 * changed these to function pointers.. it saves cycles and will allow
 * the irq dependencies to be split into different files at a later date
 * sun4c_irq.c, sun4m_irq.c etc so we could reduce the kernel size.
 * Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Changed these to btfixup entities... It saves cycles :)
 */

BTFIXUPDEF_CALL(void, disable_irq, unsigned int)
BTFIXUPDEF_CALL(void, enable_irq, unsigned int)
BTFIXUPDEF_CALL(void, disable_pil_irq, unsigned int)
BTFIXUPDEF_CALL(void, enable_pil_irq, unsigned int)
BTFIXUPDEF_CALL(void, clear_clock_irq, void)
BTFIXUPDEF_CALL(void, load_profile_irq, int, unsigned int)

static inline void __disable_irq(unsigned int irq)
{
	BTFIXUP_CALL(disable_irq)(irq);
}

static inline void __enable_irq(unsigned int irq)
{
	BTFIXUP_CALL(enable_irq)(irq);
}

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

static inline void load_profile_irq(int cpu, int limit)
{
	BTFIXUP_CALL(load_profile_irq)(cpu, limit);
}

#ifdef CONFIG_SMP
BTFIXUPDEF_CALL(void, set_cpu_int, int, int)
BTFIXUPDEF_CALL(void, clear_cpu_int, int, int)
BTFIXUPDEF_CALL(void, set_irq_udt, int)

#define set_cpu_int(cpu,level) BTFIXUP_CALL(set_cpu_int)(cpu,level)
#define clear_cpu_int(cpu,level) BTFIXUP_CALL(clear_cpu_int)(cpu,level)
#define set_irq_udt(cpu) BTFIXUP_CALL(set_irq_udt)(cpu)
#endif
