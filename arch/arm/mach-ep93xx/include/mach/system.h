/*
 * arch/arm/mach-ep93xx/include/mach/system.h
 */
static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode, const char *cmd)
{
}
