#include <linux/init.h>
#include <linux/module.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <asm/io.h>

#if 0
#define DBG(x...)	INFO(KERN_INFO x)
#else
#define DBG(x...)
#endif

#define INFO(x...)	printk(KERN_INFO x)

struct iomux_mode{
        unsigned int mode:4,
                     off:4,
                     goff:4,
                     bank:4,
                     reserve:16;
};
struct union_mode{
        union{
                struct iomux_mode mux;
                unsigned int mode;
        };
};

static inline int mode_is_valid(unsigned int mode)
{
        struct union_mode m;
	
        m.mode = mode;
	if(mode == INVALID_MODE || m.mux.bank >= GPIO_BANKS)
		return 0;
	else
		return 1;

}
int mode_to_gpio(unsigned int mode)
{
        struct union_mode m;

	if(!mode_is_valid(mode)){
		INFO("<%s> mode(0x%x) is invalid\n", __func__, mode);
		return INVALID_GPIO;
	}

        m.mode = mode;
        return PIN_BASE + m.mux.bank * 32 + (m.mux.goff - 0x0A) * 8 + m.mux.off;
}

int gpio_to_mode(int gpio)
{
        unsigned int off;
        struct union_mode m;

	if(!gpio_is_valid(gpio)){
		INFO("<%s> gpio(%d), is invalid\n", __func__, gpio);
		return INVALID_MODE;
	}

        off = gpio - PIN_BASE;
        m.mux.bank = off/32;
        m.mux.goff = (off%32)/8 + 0x0A;
        m.mux.off = off%256;


	if(!mode_is_valid(m.mode)){
		INFO("<%s> gpio(gpio%d_%x%d) is invalid\n", __func__, m.mux.bank, m.mux.goff, m.mux.off);
		return INVALID_MODE;
	}

        return m.mode;
}

#ifdef GRF_IOMUX_BASE
void iomux_set(unsigned int mode)
{
        unsigned int v, addr, mask;
        struct union_mode m;
        
        m.mode = mode;
	if(!mode_is_valid(mode)){
		INFO("<%s> mode(0x%x) is invalid\n", __func__, mode);
		return;
	}
        mask = (m.mux.mode < 2)?1:3;
        v = (m.mux.mode << (m.mux.off * 2)) + (mask << (m.mux.off * 2 + 16));
        addr = (unsigned int)GRF_IOMUX_BASE + 16 * m.mux.bank + 4 * (m.mux.goff - 0x0A);

        DBG("<%s> mode(0x%04x), reg_addr(0x%08x), set_value(0x%08x)\n", __func__, mode, addr, v);

        writel_relaxed(v, (void *)addr);
}
#else
void iomux_set(unsigned int mode)
{
	INFO("%s is not support\n", __func__);
	return;
}
#endif
EXPORT_SYMBOL(iomux_set);

void iomux_set_gpio_mode(int gpio)
{
	unsigned int mode;

	mode = gpio_to_mode(gpio);
	if(mode_is_valid(mode))
        	iomux_set(mode);
}
EXPORT_SYMBOL(iomux_set_gpio_mode);



