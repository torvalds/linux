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

int iomux_mode_to_gpio(unsigned int mode)
{
        struct union_mode m;

	if(!mode_is_valid(mode)){
		INFO("<%s> mode(0x%x) is invalid\n", __func__, mode);
		return INVALID_GPIO;
	}

        m.mode = mode;
        return PIN_BASE + m.mux.bank * 32 + (m.mux.goff - 0x0A) * 8 + m.mux.off;
}
EXPORT_SYMBOL(iomux_mode_to_gpio);

int iomux_gpio_to_mode(int gpio)
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
        m.mux.off = (off%32)%8;

	if(!mode_is_valid(m.mode)){
		INFO("<%s> gpio(gpio%d_%x%d) is invalid\n", __func__, m.mux.bank, m.mux.goff, m.mux.off);
		return INVALID_MODE;
	}

        return m.mode;
}
EXPORT_SYMBOL(iomux_gpio_to_mode);

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
        //mask = (m.mux.mode < 2)?1:3;
        mask = 3;
        v = (m.mux.mode << (m.mux.off * 2)) + (mask << (m.mux.off * 2 + 16));
        addr = (unsigned int)GRF_IOMUX_BASE + 16 * m.mux.bank + 4 * (m.mux.goff - 0x0A);

        DBG("<%s> mode(0x%04x), reg_addr(0x%08x), set_value(0x%08x)\n", __func__, mode, addr, v);

        writel_relaxed(v, (void *)addr);
}
int iomux_is_set(unsigned int mode)
{
        unsigned int v, addr, mask;
        struct union_mode m;
        
        m.mode = mode;
	if(!mode_is_valid(mode)){
		INFO("<%s> mode(0x%x) is invalid\n", __func__, mode);
		return -1;
	}
        mask = 3;
        v = (m.mux.mode << (m.mux.off * 2)) + (mask << (m.mux.off * 2 + 16));
        addr = (unsigned int)GRF_IOMUX_BASE + 16 * m.mux.bank + 4 * (m.mux.goff - 0x0A);

        if((readl_relaxed((void *)addr) & v) != 0)
		return 1;
	else if((mode & 0x03) == 0) //gpio mode
		return 1;
	else
		return 0;
}
#else
void iomux_set(unsigned int mode)
{
	INFO("%s is not support\n", __func__);
	return;
}
int iomux_is_set(unsigned int mode)
{
	INFO("%s is not support\n", __func__);
	return;
}
#endif
EXPORT_SYMBOL(iomux_set);

void iomux_set_gpio_mode(int gpio)
{
	unsigned int mode;

	mode = iomux_gpio_to_mode(gpio);
	if(mode_is_valid(mode))
        	iomux_set(mode);
}
EXPORT_SYMBOL(iomux_set_gpio_mode);

static unsigned int default_mode[] = {
#ifdef GRF_IOMUX_BASE
	#if defined(CONFIG_UART0_RK29) || (CONFIG_RK_DEBUG_UART == 0)
        UART0_SOUT, UART0_SIN,
	#ifdef CONFIG_UART0_CTS_RTS_RK29
	UART0_RTSN, UART0_CTSN,
	#endif
	#endif

	#if defined(CONFIG_UART1_RK29) || (CONFIG_RK_DEBUG_UART == 1)
	UART1_SIN, UART1_SOUT,
	#ifdef CONFIG_UART1_CTS_RTS_RK29
	UART1_CTSN, UART1_RTSN,
	#endif
	#endif

	#if defined(CONFIG_UART2_RK29) || (CONFIG_RK_DEBUG_UART == 2)
        UART2_SIN, UART2_SOUT,
	#ifdef CONFIG_UART2_CTS_RTS_RK29
	UART2_CTSN, UART2_RTSN,
	#endif
	#endif

	#if defined(CONFIG_UART3_RK29) || (CONFIG_RK_DEBUG_UART == 3)
        UART3_SIN, UART3_SOUT,
	#ifdef CONFIG_UART3_CTS_RTS_RK29
	UART3_CTSN, UART3_RTSN,
	#endif
	#endif

	#ifdef CONFIG_SPIM0_RK29
        SPI0_CLK, SPI0_TXD, SPI0_RXD, SPI0_CS0,
	#endif

	#ifdef CONFIG_SPIM1_RK29
        SPI1_CLK, SPI1_TXD, SPI1_RXD, SPI1_CS0,
	#endif

	#ifdef CONFIG_I2C0_RK30
        I2C0_SCL, I2C0_SDA,
	#endif

	#ifdef CONFIG_I2C1_RK30
        I2C1_SCL, I2C1_SDA,
	#endif

	#ifdef CONFIG_I2C2_RK30
        I2C2_SDA, I2C2_SCL,
	#endif

	#ifdef CONFIG_I2C3_RK30
        I2C3_SDA, I2C3_SCL, 
	#endif

	#ifdef CONFIG_I2C4_RK30
        I2C4_SDA, I2C4_SCL,
	#endif

	#ifdef CONFIG_RK30_VMAC
	RMII_CLKOUT, RMII_TXEN, RMII_TXD1, RMII_TXD0, RMII_RXERR, 
	RMII_CRS, RMII_RXD1, RMII_RXD0, RMII_MD, RMII_MDCLK,
	#endif
#endif
};

void __init iomux_init(void)
{
        int i, len;

        len = ARRAY_SIZE(default_mode);
        for(i = 0; i < len; i++)
                iomux_set(default_mode[i]);
	
	return;
}
EXPORT_SYMBOL(iomux_init);

