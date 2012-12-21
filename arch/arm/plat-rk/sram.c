/*
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/genalloc.h>
#include <linux/string.h> /* memcpy */
#include <asm/page.h> /* PAGE_SHIFT */
#include <asm/cputype.h>
#include <asm/mach/map.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <mach/memory.h>
#include <plat/sram.h>
#include <mach/gpio.h>

/* SRAM section definitions from the linker */
extern char __sram_code_start, __ssram_code_text, __esram_code_text;
extern char __sram_data_start, __ssram_data, __esram_data;

#if defined(CONFIG_ARCH_RK30)
#define SRAM_NONCACHED	RK30_IMEM_NONCACHED
#define SRAM_CACHED	RK30_IMEM_BASE
#define SRAM_PHYS	RK30_IMEM_PHYS
#define SRAM_SIZE	RK30_IMEM_SIZE
#elif defined(CONFIG_ARCH_RK2928)
#define SRAM_NONCACHED	RK2928_IMEM_NONCACHED
#define SRAM_CACHED	RK2928_IMEM_BASE
#define SRAM_PHYS	RK2928_IMEM_PHYS
#define SRAM_SIZE	RK2928_IMEM_SIZE
#endif

static struct map_desc sram_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long) SRAM_CACHED,
		.pfn		= __phys_to_pfn(0x0),
		.length		= SZ_1M,
		.type		= MT_MEMORY,
	},
	{
		.virtual	= (unsigned long) SRAM_NONCACHED,
		.pfn		= __phys_to_pfn(SRAM_PHYS),
		.length		= SRAM_SIZE,
		.type		= MT_MEMORY_NONCACHED,
	},
};

#define SRAM_LOG_BUF_LEN 64
static char __sramdata *sram_log_buf;
static unsigned char __sramdata sram_log_end;
#define SRAM_LOG_BUF_MASK (SRAM_LOG_BUF_LEN-1)
#define SRAM_LOG_BUF(idx) (sram_log_buf[(idx) & SRAM_LOG_BUF_MASK])

void __sramfunc sram_log_char(char c)
{
	if (!sram_log_buf)
		return;
	SRAM_LOG_BUF(sram_log_end) = c;
	sram_log_end++;
}

void __sramfunc sram_log_reset(void)
{
	int i;
	if (!sram_log_buf)
		return;
	for (i = 0; i < SRAM_LOG_BUF_LEN; i++)
		sram_log_buf[i] = 0;
	sram_log_end = 0;
}

#include <linux/ctype.h>
void __init sram_log_dump(void)
{
	int i;

	if ((u32)SRAM_DATA_END & SRAM_LOG_BUF_LEN)
		sram_log_buf = NULL;
	else
		sram_log_buf = SRAM_NONCACHED + (SRAM_DATA_END - SRAM_CACHED) + 1;
	if (!sram_log_buf)
		return;

	printk("sram_log: ");
	for (i = 0; i < SRAM_LOG_BUF_LEN; i++) {
		char c = sram_log_buf[i];
		if (isascii(c) && isprint(c))
			printk(KERN_CONT "%c", c);
		else
			printk(KERN_CONT " ");
	}
	printk(KERN_CONT "\n");
}

int __init rk29_sram_init(void)
{
	char *start;
	char *end;
	char *ram;

	iotable_init(sram_io_desc, ARRAY_SIZE(sram_io_desc));

	/*
	 * Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but since we're called from map_io(), we
	 * must do it here.
	 */
	local_flush_tlb_all();
	flush_cache_all();

        memset((char *)SRAM_CODE_OFFSET,0x0,(SRAM_CODE_END - SRAM_CODE_OFFSET + 1));
	memset((char *)SRAM_DATA_OFFSET,0x0,(SRAM_DATA_END - SRAM_DATA_OFFSET + 1));

	/* Copy code from RAM to SRAM CODE */
	start = &__ssram_code_text;
	end   = &__esram_code_text;
	ram   = &__sram_code_start;
	memcpy(start, ram, (end-start));
	flush_icache_range((unsigned long) start, (unsigned long) end);

	printk("CPU SRAM: copied sram code from %p to %p - %p\n", ram, start, end);

	/* Copy data from RAM to SRAM DATA */
	start = &__ssram_data;
	end   = &__esram_data;
	ram   = &__sram_data_start;
	memcpy(start, ram, (end-start));

	printk("CPU SRAM: copied sram data from %p to %p - %p\n", ram, start, end);

	sram_log_dump();

	return 0;
}

__weak void __sramfunc sram_printch(char byte)
{
	sram_log_char(byte);
#ifdef DEBUG_UART_BASE
	writel_relaxed(byte, DEBUG_UART_BASE);
	dsb();

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl_relaxed(DEBUG_UART_BASE + 0x14) & 0x40))
		barrier();

	if (byte == '\n')
		sram_printch('\r');
#endif
}

void __sramfunc sram_printascii(const char *s)
{
	while (*s) {
		sram_printch(*s);
		s++;
	}
}

void __sramfunc sram_printhex(unsigned int hex)
{
	int i = 8;
	sram_printch('0');
	sram_printch('x');
	while (i--) {
		unsigned char c = (hex & 0xF0000000) >> 28;
		sram_printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}

struct sram_gpio_data __sramdata pmic_sleep;
#if defined(CONFIG_ARCH_RK2928)
static void __iomem *gpio_base[] = {RK2928_GPIO0_BASE, RK2928_GPIO1_BASE, RK2928_GPIO2_BASE, RK2928_GPIO3_BASE};
#elif defined(CONFIG_ARCH_RK3066B)
static void __iomem *gpio_base[] = {RK30_GPIO0_BASE, RK30_GPIO1_BASE, RK30_GPIO2_BASE, RK30_GPIO3_BASE};
#elif defined(CONFIG_ARCH_RK30)
static void __iomem *gpio_base[] = {RK30_GPIO0_BASE, RK30_GPIO1_BASE, RK30_GPIO2_BASE, RK30_GPIO3_BASE, RK30_GPIO4_BASE, RK30_GPIO6_BASE};
#endif

int sram_gpio_init(int gpio, struct sram_gpio_data *data)
{
       unsigned index;

       if(gpio == INVALID_GPIO)
               return -EINVAL;
       index = gpio - PIN_BASE;
       if(index/NUM_GROUP >= ARRAY_SIZE(gpio_base))
               return -EINVAL;

       data->base = gpio_base[index/NUM_GROUP];
       data->offset = index%NUM_GROUP;

       return 0;
}

void __sramfunc sram_gpio_set_value(struct sram_gpio_data data, uint value)
{
       writel_relaxed(readl_relaxed(data.base + GPIO_SWPORTA_DDR)| (1<<data.offset),
                       data.base + GPIO_SWPORTA_DDR);
       if(value)
               writel_relaxed(readl_relaxed(data.base + GPIO_SWPORTA_DR) | (1<<data.offset),
                               data.base + GPIO_SWPORTA_DR);
       else
               writel_relaxed(readl_relaxed(data.base + GPIO_SWPORTA_DR) & ~(1<<data.offset),
                               data.base + GPIO_SWPORTA_DR);
}


