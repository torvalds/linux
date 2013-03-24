#ifndef __GPIO_SUNXI_H__
#define __GPIO_SUNXI_H__

#define SUNXI_GPIO_VER		"1.3"
#define MAX_GPIO_NAMELEN	16

/* Allwinner A1X gpio mode numbers */
#define SUNXI_GPIO_INPUT	0
#define SUNXI_GPIO_OUTPUT	1

/* PIO controller defines */
#define PIO_BASE_ADDRESS	SW_PA_PORTC_IO_BASE
#define PIO_RANGE_SIZE		(0x400)
#define PIO_INT_STAT_OFFSET	(0x214)
#define PIO_INT_CTRL_OFFSET	(0x210)

/* EINT type defines */
#define POSITIVE_EDGE		0x0
#define NEGATIVE_EDGE		0x1
#define HIGH_LEVEL		0x2
#define LOW_LEVEL		0x3
#define DOUBLE_EDGE		0x4

/* EINT type PIO controller registers */
#define PIO_INT_CFG0_OFFSET	0x200
#define PIO_INT_CFG1_OFFSET	0x204
#define PIO_INT_CFG2_OFFSET	0x208
#define PIO_INT_CFG3_OFFSET	0x20c

static int int_cfg_addr[] = {PIO_INT_CFG0_OFFSET,
			     PIO_INT_CFG1_OFFSET,
			     PIO_INT_CFG2_OFFSET,
			     PIO_INT_CFG3_OFFSET};

struct gpio_eint_data {
	int port;		/* gpio port number  */
	int pin;		/* gpio pin number   */
	int mux;		/* pin mux mode 0-7  */
	int gpio;		/* irq_to_gpio field */
};

struct sunxi_gpio_data {
	unsigned gpio_handler;
	script_gpio_set_t info;
	int eint;
	int eint_mux;
	char pin_name[16];
};

struct sunxi_gpio_chip {
	struct gpio_chip chip;
	struct sunxi_gpio_data *data;
	struct device *dev;
	int irq_base;
	int gpio_num;
	void __iomem *gaddr;
	spinlock_t irq_lock;
};

/* Clear pending GPIO interrupt */
#define SUNXI_CLEAR_EINT(addr, irq) ({ \
	writel((__u32)(1 << irq), addr + PIO_INT_STAT_OFFSET); \
})

/* Disable GPIO interrupt for pin */
#define SUNXI_MASK_GPIO_IRQ(addr, irq) ({ \
	__u32 reg_val = readl(addr + PIO_INT_CTRL_OFFSET); \
	reg_val &= ~(1 << irq); \
	writel(reg_val, addr + PIO_INT_CTRL_OFFSET); \
})

/* Enable GPIO interrupt for pin */
#define SUNXI_UNMASK_GPIO_IRQ(addr, irq) ({ \
	__u32 reg_val = readl(addr + PIO_INT_CTRL_OFFSET); \
	reg_val |= (1 << irq); \
	writel(reg_val, addr + PIO_INT_CTRL_OFFSET); \
})

/* Setup GPIO irq mode (FALLING, RISING, BOTH, etc */
#define SUNXI_SET_GPIO_IRQ_TYPE(addr, offs, mode) ({ \
	__u32 reg_bit = offs % 8; \
	__u32 reg_num = offs / 8; \
	__u32 reg_val = readl(addr + int_cfg_addr[reg_num]); \
	reg_val &= (~(0xf << (reg_bit * 4))); \
	reg_val |= (mode << (reg_bit * 4)); \
	writel(reg_val, addr + int_cfg_addr[reg_num]); \
})

/* Set GPIO pin mode (input, output, etc)            */
/* GPIO port has 4 cfg 32bit registers (8 pins each) */
/* First port cfg register addr = port_num * 0x24    */
#define SUNXI_SET_GPIO_MODE(addr, port, pin, mode) ({ \
	__u32 reg_val = 0; \
	__u32 pin_idx = pin >> 3; \
	void *raddr = addr + (((port)-1)*0x24 + ((pin_idx)<<2) + 0x00); \
	reg_val = readl(raddr); \
	reg_val &= ~(0x07 << (((pin - (pin_idx<<3))<<2))); \
	reg_val |= mode << (((pin - (pin_idx<<3))<<2)); \
	writel(reg_val, raddr); \
})

#endif /* __GPIO_SUNXI_H__ */
