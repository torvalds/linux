#ifndef __ASM_MACH_UNCOMPRESS_H
#define __ASM_MACH_UNCOMPRESS_H

/*
 * This does not append a newline
 */
static void putc(int c)
{
}

static inline void flush(void)
{
}

static void arch_decomp_setup(void)
{
}

#define arch_decomp_wdog()

#endif /* __ASM_MACH_UNCOMPRESS_H */
