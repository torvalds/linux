#ifndef __MACH_ROCKCHIP_SRAM_H
#define __MACH_ROCKCHIP_SRAM_H

#include <linux/pie.h>
#include <asm/pie.h>

extern char __pie_common_start[];
extern char __pie_common_end[];
extern char __pie_overlay_start[];

extern struct gen_pool *rockchip_sram_pool;
extern struct pie_chunk *rockchip_pie_chunk;
extern void *rockchip_sram_virt;
extern size_t rockchip_sram_size;
extern char *rockchip_sram_stack;
extern char __pie_data(rk3188) __pie_rk3188_sram_stack[1024];

#define RK_PIE_DATA3(x) __pie_data(rk##x)
#define RK_PIE_DATA2(x) RK_PIE_DATA3(x)
#define RK_PIE_DATA RK_PIE_DATA2(CPU)

#define DATA3(x, y) __pie_rk##y##_##x
#define DATA2(x, y) DATA3(x, y)
#define DATA(x) DATA2(x, CPU)

#define RK_PIE3(x) __pie(rk##x)
#define RK_PIE2(x) RK_PIE3(x)
#define RK_PIE RK_PIE2(CPU)

#define FUNC3(x, y) __pie_rk##y##_##x
#define FUNC2(x, y) FUNC3(x, y)
#define FUNC(x) FUNC2(x, CPU)

#define PIE_FUNC(x) RK_PIE FUNC(x)
#define PIE_DATA(x) RK_PIE_DATA DATA(x)

#define DEFINE_PIE_DATA(x) PIE_DATA(x); EXPORT_PIE_SYMBOL(DATA(x));

/* Tag variables with this */
#define __sramdata RK_PIE_DATA
/* Tag functions inside SRAM called from outside SRAM with this */
#define __sramfunc RK_PIE noinline
/* Tag function inside SRAM called from inside SRAM  with this */
#define __sramlocalfunc RK_PIE

extern void call_with_stack(void (*fn)(void *), void *arg, void *sp);

#ifdef CONFIG_PIE
extern int __init rockchip_pie_init(void);
#else
static inline int rockchip_pie_init(void) { return -1; }
#endif

#endif
