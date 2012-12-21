#ifndef __PLAT_SRAM_H
#define __PLAT_SRAM_H

#ifdef CONFIG_PLAT_RK

#include <linux/init.h>

/* Tag variables with this */
#define __sramdata __section(.sram.data)
/* Tag constants with this */
#define __sramconst __section(.sram.rodata)
/* Tag functions inside SRAM called from outside SRAM with this */
#define __sramfunc __attribute__((long_call)) __section(.sram.text) noinline
/* Tag function inside SRAM called from inside SRAM  with this */
#define __sramlocalfunc __section(.sram.text)

#define GPIO_SWPORTA_DR  0x0000
#define GPIO_SWPORTA_DDR 0x0004

struct sram_gpio_data {
       void __iomem *base;
       uint offset;
};

extern struct sram_gpio_data __sramdata pmic_sleep;
int sram_gpio_init(int gpio, struct sram_gpio_data *data);
void __sramfunc sram_gpio_set_value(struct sram_gpio_data data, uint value);

int __init rk29_sram_init(void);

static inline unsigned long ddr_save_sp(unsigned long new_sp)
{
	unsigned long old_sp;

	asm volatile ("mov %0, sp" : "=r" (old_sp));
	asm volatile ("mov sp, %0" :: "r" (new_sp));
	return old_sp;
}

// save_sp 必须定义为全局变量
#define DDR_SAVE_SP(save_sp)		do { save_sp = ddr_save_sp(((unsigned long)SRAM_DATA_END & (~7))); } while (0)
#define DDR_RESTORE_SP(save_sp)		do { ddr_save_sp(save_sp); } while (0)

extern void __sramfunc sram_printch(char byte);
extern void __sramfunc sram_printascii(const char *s);
extern void __sramfunc sram_printhex(unsigned int hex);
extern void __sramfunc sram_log_char(char c);
extern void __sramfunc sram_log_reset(void);

#endif /* CONFIG_PLAT_RK */
#endif

