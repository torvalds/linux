// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Nuvoton Technology corporation.

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

/* GCR registers */
#define NPCM8XX_GCR_SRCNT	0x068
#define NPCM8XX_GCR_FLOCKR1	0x074
#define NPCM8XX_GCR_DSCNT	0x078
#define NPCM8XX_GCR_I2CSEGSEL	0x0e0
#define NPCM8XX_GCR_MFSEL1	0x260
#define NPCM8XX_GCR_MFSEL2	0x264
#define NPCM8XX_GCR_MFSEL3	0x268
#define NPCM8XX_GCR_MFSEL4	0x26c
#define NPCM8XX_GCR_MFSEL5	0x270
#define NPCM8XX_GCR_MFSEL6	0x274
#define NPCM8XX_GCR_MFSEL7	0x278

#define SRCNT_ESPI		BIT(3)

/* GPIO registers */
#define NPCM8XX_GP_N_TLOCK1	0x00
#define NPCM8XX_GP_N_DIN	0x04
#define NPCM8XX_GP_N_POL	0x08
#define NPCM8XX_GP_N_DOUT	0x0c
#define NPCM8XX_GP_N_OE		0x10
#define NPCM8XX_GP_N_OTYP	0x14
#define NPCM8XX_GP_N_MP		0x18
#define NPCM8XX_GP_N_PU		0x1c
#define NPCM8XX_GP_N_PD		0x20
#define NPCM8XX_GP_N_DBNC	0x24
#define NPCM8XX_GP_N_EVTYP	0x28
#define NPCM8XX_GP_N_EVBE	0x2c
#define NPCM8XX_GP_N_OBL0	0x30
#define NPCM8XX_GP_N_OBL1	0x34
#define NPCM8XX_GP_N_OBL2	0x38
#define NPCM8XX_GP_N_OBL3	0x3c
#define NPCM8XX_GP_N_EVEN	0x40
#define NPCM8XX_GP_N_EVENS	0x44
#define NPCM8XX_GP_N_EVENC	0x48
#define NPCM8XX_GP_N_EVST	0x4c
#define NPCM8XX_GP_N_SPLCK	0x50
#define NPCM8XX_GP_N_MPLCK	0x54
#define NPCM8XX_GP_N_IEM	0x58
#define NPCM8XX_GP_N_OSRC	0x5c
#define NPCM8XX_GP_N_ODSC	0x60
#define NPCM8XX_GP_N_DOS	0x68
#define NPCM8XX_GP_N_DOC	0x6c
#define NPCM8XX_GP_N_OES	0x70
#define NPCM8XX_GP_N_OEC	0x74
#define NPCM8XX_GP_N_DBNCS0	0x80
#define NPCM8XX_GP_N_DBNCS1	0x84
#define NPCM8XX_GP_N_DBNCP0	0x88
#define NPCM8XX_GP_N_DBNCP1	0x8c
#define NPCM8XX_GP_N_DBNCP2	0x90
#define NPCM8XX_GP_N_DBNCP3	0x94
#define NPCM8XX_GP_N_TLOCK2	0xac

#define NPCM8XX_GPIO_PER_BANK	32
#define NPCM8XX_GPIO_BANK_NUM	8
#define NPCM8XX_GCR_NONE	0

#define NPCM8XX_DEBOUNCE_MAX		4
#define NPCM8XX_DEBOUNCE_NSEC		40
#define NPCM8XX_DEBOUNCE_VAL_MASK	GENMASK(23, 4)
#define NPCM8XX_DEBOUNCE_MAX_VAL	0xFFFFF7

/* Structure for register banks */
struct debounce_time {
	bool	set_val[NPCM8XX_DEBOUNCE_MAX];
	u32	nanosec_val[NPCM8XX_DEBOUNCE_MAX];
};

struct npcm8xx_gpio {
	struct gpio_chip	gc;
	void __iomem		*base;
	struct debounce_time	debounce;
	int			irqbase;
	int			irq;
	struct irq_chip		irq_chip;
	u32			pinctrl_id;
	int (*direction_input)(struct gpio_chip *chip, unsigned int offset);
	int (*direction_output)(struct gpio_chip *chip, unsigned int offset,
				int value);
	int (*request)(struct gpio_chip *chip, unsigned int offset);
	void (*free)(struct gpio_chip *chip, unsigned int offset);
};

struct npcm8xx_pinctrl {
	struct pinctrl_dev	*pctldev;
	struct device		*dev;
	struct npcm8xx_gpio	gpio_bank[NPCM8XX_GPIO_BANK_NUM];
	struct irq_domain	*domain;
	struct regmap		*gcr_regmap;
	void __iomem		*regs;
	u32			bank_num;
};

/* GPIO handling in the pinctrl driver */
static void npcm_gpio_set(struct gpio_chip *gc, void __iomem *reg,
			  unsigned int pinmask)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	iowrite32(ioread32(reg) | pinmask, reg);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void npcm_gpio_clr(struct gpio_chip *gc, void __iomem *reg,
			  unsigned int pinmask)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	iowrite32(ioread32(reg) & ~pinmask, reg);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void npcmgpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct npcm8xx_gpio *bank = gpiochip_get_data(chip);

	seq_printf(s, "DIN :%.8x DOUT:%.8x IE  :%.8x OE	 :%.8x\n",
		   ioread32(bank->base + NPCM8XX_GP_N_DIN),
		   ioread32(bank->base + NPCM8XX_GP_N_DOUT),
		   ioread32(bank->base + NPCM8XX_GP_N_IEM),
		   ioread32(bank->base + NPCM8XX_GP_N_OE));
	seq_printf(s, "PU  :%.8x PD  :%.8x DB  :%.8x POL :%.8x\n",
		   ioread32(bank->base + NPCM8XX_GP_N_PU),
		   ioread32(bank->base + NPCM8XX_GP_N_PD),
		   ioread32(bank->base + NPCM8XX_GP_N_DBNC),
		   ioread32(bank->base + NPCM8XX_GP_N_POL));
	seq_printf(s, "ETYP:%.8x EVBE:%.8x EVEN:%.8x EVST:%.8x\n",
		   ioread32(bank->base + NPCM8XX_GP_N_EVTYP),
		   ioread32(bank->base + NPCM8XX_GP_N_EVBE),
		   ioread32(bank->base + NPCM8XX_GP_N_EVEN),
		   ioread32(bank->base + NPCM8XX_GP_N_EVST));
	seq_printf(s, "OTYP:%.8x OSRC:%.8x ODSC:%.8x\n",
		   ioread32(bank->base + NPCM8XX_GP_N_OTYP),
		   ioread32(bank->base + NPCM8XX_GP_N_OSRC),
		   ioread32(bank->base + NPCM8XX_GP_N_ODSC));
	seq_printf(s, "OBL0:%.8x OBL1:%.8x OBL2:%.8x OBL3:%.8x\n",
		   ioread32(bank->base + NPCM8XX_GP_N_OBL0),
		   ioread32(bank->base + NPCM8XX_GP_N_OBL1),
		   ioread32(bank->base + NPCM8XX_GP_N_OBL2),
		   ioread32(bank->base + NPCM8XX_GP_N_OBL3));
	seq_printf(s, "SLCK:%.8x MLCK:%.8x\n",
		   ioread32(bank->base + NPCM8XX_GP_N_SPLCK),
		   ioread32(bank->base + NPCM8XX_GP_N_MPLCK));
}

static int npcmgpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct npcm8xx_gpio *bank = gpiochip_get_data(chip);
	int ret;

	ret = pinctrl_gpio_direction_input(chip, offset);
	if (ret)
		return ret;

	return bank->direction_input(chip, offset);
}

static int npcmgpio_direction_output(struct gpio_chip *chip,
				     unsigned int offset, int value)
{
	struct npcm8xx_gpio *bank = gpiochip_get_data(chip);
	int ret;

	ret = pinctrl_gpio_direction_output(chip, offset);
	if (ret)
		return ret;

	return bank->direction_output(chip, offset, value);
}

static int npcmgpio_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct npcm8xx_gpio *bank = gpiochip_get_data(chip);
	int ret;

	ret = pinctrl_gpio_request(chip, offset);
	if (ret)
		return ret;

	return bank->request(chip, offset);
}

static void npcmgpio_irq_handler(struct irq_desc *desc)
{
	unsigned long sts, en, bit;
	struct npcm8xx_gpio *bank;
	struct irq_chip *chip;
	struct gpio_chip *gc;

	gc = irq_desc_get_handler_data(desc);
	bank = gpiochip_get_data(gc);
	chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);
	sts = ioread32(bank->base + NPCM8XX_GP_N_EVST);
	en  = ioread32(bank->base + NPCM8XX_GP_N_EVEN);
	sts &= en;
	for_each_set_bit(bit, &sts, NPCM8XX_GPIO_PER_BANK)
		generic_handle_domain_irq(gc->irq.domain, bit);
	chained_irq_exit(chip, desc);
}

static int npcmgpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct npcm8xx_gpio *bank =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int gpio = BIT(irqd_to_hwirq(d));

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_EVBE, gpio);
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_POL, gpio);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_EVBE, gpio);
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_POL, gpio);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_POL, gpio);
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_EVBE, gpio);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_POL, gpio);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_POL, gpio);
		break;
	default:
		return -EINVAL;
	}

	if (type & IRQ_TYPE_LEVEL_MASK) {
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_EVTYP, gpio);
		irq_set_handler_locked(d, handle_level_irq);
	} else if (type & IRQ_TYPE_EDGE_BOTH) {
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_EVTYP, gpio);
		irq_set_handler_locked(d, handle_edge_irq);
	}

	return 0;
}

static void npcmgpio_irq_ack(struct irq_data *d)
{
	struct npcm8xx_gpio *bank =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int gpio = irqd_to_hwirq(d);

	iowrite32(BIT(gpio), bank->base + NPCM8XX_GP_N_EVST);
}

static void npcmgpio_irq_mask(struct irq_data *d)
{
	struct npcm8xx_gpio *bank =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int gpio = irqd_to_hwirq(d);

	iowrite32(BIT(gpio), bank->base + NPCM8XX_GP_N_EVENC);
}

static void npcmgpio_irq_unmask(struct irq_data *d)
{
	struct npcm8xx_gpio *bank =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int gpio = irqd_to_hwirq(d);

	iowrite32(BIT(gpio), bank->base + NPCM8XX_GP_N_EVENS);
}

static unsigned int npcmgpio_irq_startup(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irqd_to_hwirq(d);

	/* active-high, input, clear interrupt, enable interrupt */
	npcmgpio_direction_input(gc, gpio);
	npcmgpio_irq_ack(d);
	npcmgpio_irq_unmask(d);

	return 0;
}

static struct irq_chip npcmgpio_irqchip = {
	.name = "NPCM8XX-GPIO-IRQ",
	.irq_ack = npcmgpio_irq_ack,
	.irq_unmask = npcmgpio_irq_unmask,
	.irq_mask = npcmgpio_irq_mask,
	.irq_set_type = npcmgpio_set_irq_type,
	.irq_startup = npcmgpio_irq_startup,
	.flags =  IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static const int gpi36_pins[] = { 36 };
static const int gpi35_pins[] = { 35 };

static const int tp_jtag3_pins[] = { 44, 62, 45, 46 };
static const int tp_uart_pins[] = { 50, 51 };

static const int tp_smb2_pins[] = { 24, 25 };
static const int tp_smb1_pins[] = { 142, 143 };

static const int tp_gpio7_pins[] = { 96 };
static const int tp_gpio6_pins[] = { 97 };
static const int tp_gpio5_pins[] = { 98 };
static const int tp_gpio4_pins[] = { 99 };
static const int tp_gpio3_pins[] = { 100 };
static const int tp_gpio2_pins[] = { 16 };
static const int tp_gpio1_pins[] = { 9 };
static const int tp_gpio0_pins[] = { 8 };

static const int tp_gpio2b_pins[] = { 101 };
static const int tp_gpio1b_pins[] = { 92 };
static const int tp_gpio0b_pins[] = { 91 };

static const int vgadig_pins[] = { 102, 103, 104, 105 };

static const int nbu1crts_pins[] = { 44, 62 };

static const int fm2_pins[] = { 224, 225, 226, 227, 228, 229, 230 };
static const int fm1_pins[] = { 175, 176, 177, 203, 191, 192, 233 };
static const int fm0_pins[] = { 194, 195, 196, 202, 199, 198, 197 };

static const int gpio1836_pins[] = { 183, 184, 185, 186 };
static const int gpio1889_pins[] = { 188, 189 };
static const int gpo187_pins[] = { 187 };

static const int cp1urxd_pins[] = { 41 };
static const int r3rxer_pins[] = { 212 };

static const int cp1gpio2c_pins[] = { 101 };
static const int cp1gpio3c_pins[] = { 100 };

static const int cp1gpio0b_pins[] = { 127 };
static const int cp1gpio1b_pins[] = { 126 };
static const int cp1gpio2b_pins[] = { 125 };
static const int cp1gpio3b_pins[] = { 124 };
static const int cp1gpio4b_pins[] = { 99 };
static const int cp1gpio5b_pins[] = { 98 };
static const int cp1gpio6b_pins[] = { 97 };
static const int cp1gpio7b_pins[] = { 96 };

static const int cp1gpio0_pins[] = {  };
static const int cp1gpio1_pins[] = {  };
static const int cp1gpio2_pins[] = {  };
static const int cp1gpio3_pins[] = {  };
static const int cp1gpio4_pins[] = {  };
static const int cp1gpio5_pins[] = { 17 };
static const int cp1gpio6_pins[] = { 91 };
static const int cp1gpio7_pins[] = { 92 };

static const int cp1utxd_pins[] = { 42 };

static const int spi1cs3_pins[] = { 192 };
static const int spi1cs2_pins[] = { 191 };
static const int spi1cs1_pins[] = { 233 };
static const int spi1cs0_pins[] = { 203 };

static const int spi1d23_pins[] = { 191, 192 };

static const int j2j3_pins[] = { 44, 62, 45, 46 };

static const int r3oen_pins[] = { 213 };
static const int r2oen_pins[] = { 90 };
static const int r1oen_pins[] = { 56 };
static const int bu4b_pins[] = { 98, 99 };
static const int bu4_pins[] = { 54, 55 };
static const int bu5b_pins[] = { 100, 101 };
static const int bu5_pins[] = { 52, 53 };
static const int bu6_pins[] = { 50, 51 };
static const int rmii3_pins[] = { 110, 111, 209, 211, 210, 214, 215 };

static const int jm1_pins[] = { 136, 137, 138, 139, 140 };
static const int jm2_pins[] = { 251 };

static const int tpgpio5b_pins[] = { 58 };
static const int tpgpio4b_pins[] = { 57 };

static const int clkrun_pins[] = { 162 };

static const int i3c5_pins[] = { 106, 107 };
static const int i3c4_pins[] = { 33, 34 };
static const int i3c3_pins[] = { 246, 247 };
static const int i3c2_pins[] = { 244, 245 };
static const int i3c1_pins[] = { 242, 243 };
static const int i3c0_pins[] = { 240, 241 };

static const int hsi1a_pins[] = { 43, 63 };
static const int hsi2a_pins[] = { 48, 49 };
static const int hsi1b_pins[] = { 44, 62 };
static const int hsi2b_pins[] = { 50, 51 };
static const int hsi1c_pins[] = { 45, 46, 47, 61 };
static const int hsi2c_pins[] = { 45, 46, 47, 61 };

static const int smb0_pins[]  = { 115, 114 };
static const int smb0b_pins[] = { 195, 194 };
static const int smb0c_pins[] = { 202, 196 };
static const int smb0d_pins[] = { 198, 199 };
static const int smb0den_pins[] = { 197 };
static const int smb1_pins[]  = { 117, 116 };
static const int smb1b_pins[] = { 126, 127 };
static const int smb1c_pins[] = { 124, 125 };
static const int smb1d_pins[] = { 4, 5 };
static const int smb2_pins[]  = { 119, 118 };
static const int smb2b_pins[] = { 122, 123 };
static const int smb2c_pins[] = { 120, 121 };
static const int smb2d_pins[] = { 6, 7 };
static const int smb3_pins[]  = { 30, 31 };
static const int smb3b_pins[] = { 39, 40 };
static const int smb3c_pins[] = { 37, 38 };
static const int smb3d_pins[] = { 59, 60 };
static const int smb4_pins[]  = { 28, 29 };
static const int smb4b_pins[] = { 18, 19 };
static const int smb4c_pins[] = { 20, 21 };
static const int smb4d_pins[] = { 22, 23 };
static const int smb5_pins[]  = { 26, 27 };
static const int smb5b_pins[] = { 13, 12 };
static const int smb5c_pins[] = { 15, 14 };
static const int smb5d_pins[] = { 94, 93 };
static const int ga20kbc_pins[] = { 94, 93 };

static const int smb6_pins[]  = { 172, 171 };
static const int smb6b_pins[] = { 2, 3 };
static const int smb6c_pins[]  = { 0, 1 };
static const int smb6d_pins[]  = { 10, 11 };
static const int smb7_pins[]  = { 174, 173 };
static const int smb7b_pins[]  = { 16, 141 };
static const int smb7c_pins[]  = { 24, 25 };
static const int smb7d_pins[]  = { 142, 143 };
static const int smb8_pins[]  = { 129, 128 };
static const int smb9_pins[]  = { 131, 130 };
static const int smb10_pins[] = { 133, 132 };
static const int smb11_pins[] = { 135, 134 };
static const int smb12_pins[] = { 221, 220 };
static const int smb13_pins[] = { 223, 222 };
static const int smb14_pins[] = { 22, 23 };
static const int smb14b_pins[] = { 32, 187 };
static const int smb15_pins[] = { 20, 21 };
static const int smb15b_pins[] = { 192, 191 };

static const int smb16_pins[] = { 10, 11 };
static const int smb16b_pins[] = { 218, 219 };
static const int smb17_pins[] = { 3, 2 };
static const int smb18_pins[] = { 0, 1 };
static const int smb19_pins[] = { 60, 59 };
static const int smb20_pins[] = { 234, 235 };
static const int smb21_pins[] = { 169, 170 };
static const int smb22_pins[] = { 40, 39 };
static const int smb23_pins[] = { 38, 37 };
static const int smb23b_pins[] = { 134, 135 };

static const int fanin0_pins[] = { 64 };
static const int fanin1_pins[] = { 65 };
static const int fanin2_pins[] = { 66 };
static const int fanin3_pins[] = { 67 };
static const int fanin4_pins[] = { 68 };
static const int fanin5_pins[] = { 69 };
static const int fanin6_pins[] = { 70 };
static const int fanin7_pins[] = { 71 };
static const int fanin8_pins[] = { 72 };
static const int fanin9_pins[] = { 73 };
static const int fanin10_pins[] = { 74 };
static const int fanin11_pins[] = { 75 };
static const int fanin12_pins[] = { 76 };
static const int fanin13_pins[] = { 77 };
static const int fanin14_pins[] = { 78 };
static const int fanin15_pins[] = { 79 };
static const int faninx_pins[] = { 175, 176, 177, 203 };

static const int pwm0_pins[] = { 80 };
static const int pwm1_pins[] = { 81 };
static const int pwm2_pins[] = { 82 };
static const int pwm3_pins[] = { 83 };
static const int pwm4_pins[] = { 144 };
static const int pwm5_pins[] = { 145 };
static const int pwm6_pins[] = { 146 };
static const int pwm7_pins[] = { 147 };
static const int pwm8_pins[] = { 220 };
static const int pwm9_pins[] = { 221 };
static const int pwm10_pins[] = { 234 };
static const int pwm11_pins[] = { 235 };

static const int uart1_pins[] = { 43, 45, 46, 47, 61, 62, 63 };
static const int uart2_pins[] = { 48, 49, 50, 51, 52, 53, 54, 55 };

static const int sg1mdio_pins[] = { 108, 109 };

static const int rg2_pins[] = { 110, 111, 112, 113, 208, 209, 210, 211, 212,
	213, 214, 215 };
static const int rg2mdio_pins[] = { 216, 217 };

static const int ddr_pins[] = { 110, 111, 112, 113, 208, 209, 210, 211, 212,
	213, 214, 215, 216, 217, 250 };

static const int iox1_pins[] = { 0, 1, 2, 3 };
static const int iox2_pins[] = { 4, 5, 6, 7 };
static const int ioxh_pins[] = { 10, 11, 24, 25 };

static const int mmc_pins[] = { 152, 154, 156, 157, 158, 159 };
static const int mmcwp_pins[] = { 153 };
static const int mmccd_pins[] = { 155 };
static const int mmcrst_pins[] = { 155 };
static const int mmc8_pins[] = { 148, 149, 150, 151 };

static const int r1_pins[] = { 178, 179, 180, 181, 182, 193, 201 };
static const int r1err_pins[] = { 56 };
static const int r1md_pins[] = { 57, 58 };
static const int r2_pins[] = { 84, 85, 86, 87, 88, 89, 200 };
static const int r2err_pins[] = { 90 };
static const int r2md_pins[] = { 91, 92 };
static const int sd1_pins[] = { 136, 137, 138, 139, 140, 141, 142, 143 };
static const int sd1pwr_pins[] = { 143 };

static const int wdog1_pins[] = { 218 };
static const int wdog2_pins[] = { 219 };

static const int bmcuart0a_pins[] = { 41, 42 };
static const int bmcuart0b_pins[] = { 48, 49 };
static const int bmcuart1_pins[] = { 43, 44, 62, 63 };

static const int scipme_pins[] = { 169 };
static const int smi_pins[] = { 170 };
static const int serirq_pins[] = { 168 };

static const int clkout_pins[] = { 160 };
static const int clkreq_pins[] = { 231 };

static const int jtag2_pins[] = { 43, 44, 45, 46, 47 };
static const int gspi_pins[] = { 12, 13, 14, 15 };

static const int spix_pins[] = { 224, 225, 226, 227, 229, 230 };
static const int spixcs1_pins[] = { 228 };

static const int spi1_pins[] = { 175, 176, 177 };
static const int pspi_pins[] = { 17, 18, 19 };

static const int spi0cs1_pins[] = { 32 };

static const int spi3_pins[] = { 183, 184, 185, 186 };
static const int spi3cs1_pins[] = { 187 };
static const int spi3quad_pins[] = { 188, 189 };
static const int spi3cs2_pins[] = { 188 };
static const int spi3cs3_pins[] = { 189 };

static const int ddc_pins[] = { 204, 205, 206, 207 };

static const int lpc_pins[] = { 95, 161, 163, 164, 165, 166, 167 };
static const int espi_pins[] = { 95, 161, 163, 164, 165, 166, 167, 168 };

static const int lkgpo0_pins[] = { 16 };
static const int lkgpo1_pins[] = { 8 };
static const int lkgpo2_pins[] = { 9 };

static const int nprd_smi_pins[] = { 190 };

static const int hgpio0_pins[] = { 20 };
static const int hgpio1_pins[] = { 21 };
static const int hgpio2_pins[] = { 22 };
static const int hgpio3_pins[] = { 23 };
static const int hgpio4_pins[] = { 24 };
static const int hgpio5_pins[] = { 25 };
static const int hgpio6_pins[] = { 59 };
static const int hgpio7_pins[] = { 60 };

/*
 * pin:	     name, number
 * group:    name, npins,   pins
 * function: name, ngroups, groups
 */
struct npcm8xx_pingroup {
	const char *name;
	const unsigned int *pins;
	int npins;
};

#define NPCM8XX_GRPS \
	NPCM8XX_GRP(gpi36), \
	NPCM8XX_GRP(gpi35), \
	NPCM8XX_GRP(tp_jtag3), \
	NPCM8XX_GRP(tp_uart), \
	NPCM8XX_GRP(tp_smb2), \
	NPCM8XX_GRP(tp_smb1), \
	NPCM8XX_GRP(tp_gpio7), \
	NPCM8XX_GRP(tp_gpio6), \
	NPCM8XX_GRP(tp_gpio5), \
	NPCM8XX_GRP(tp_gpio4), \
	NPCM8XX_GRP(tp_gpio3), \
	NPCM8XX_GRP(tp_gpio2), \
	NPCM8XX_GRP(tp_gpio1), \
	NPCM8XX_GRP(tp_gpio0), \
	NPCM8XX_GRP(tp_gpio2b), \
	NPCM8XX_GRP(tp_gpio1b), \
	NPCM8XX_GRP(tp_gpio0b), \
	NPCM8XX_GRP(vgadig), \
	NPCM8XX_GRP(nbu1crts), \
	NPCM8XX_GRP(fm2), \
	NPCM8XX_GRP(fm1), \
	NPCM8XX_GRP(fm0), \
	NPCM8XX_GRP(gpio1836), \
	NPCM8XX_GRP(gpio1889), \
	NPCM8XX_GRP(gpo187), \
	NPCM8XX_GRP(cp1urxd), \
	NPCM8XX_GRP(r3rxer), \
	NPCM8XX_GRP(cp1gpio2c), \
	NPCM8XX_GRP(cp1gpio3c), \
	NPCM8XX_GRP(cp1gpio0b), \
	NPCM8XX_GRP(cp1gpio1b), \
	NPCM8XX_GRP(cp1gpio2b), \
	NPCM8XX_GRP(cp1gpio3b), \
	NPCM8XX_GRP(cp1gpio4b), \
	NPCM8XX_GRP(cp1gpio5b), \
	NPCM8XX_GRP(cp1gpio6b), \
	NPCM8XX_GRP(cp1gpio7b), \
	NPCM8XX_GRP(cp1gpio0), \
	NPCM8XX_GRP(cp1gpio1), \
	NPCM8XX_GRP(cp1gpio2), \
	NPCM8XX_GRP(cp1gpio3), \
	NPCM8XX_GRP(cp1gpio4), \
	NPCM8XX_GRP(cp1gpio5), \
	NPCM8XX_GRP(cp1gpio6), \
	NPCM8XX_GRP(cp1gpio7), \
	NPCM8XX_GRP(cp1utxd), \
	NPCM8XX_GRP(spi1cs3), \
	NPCM8XX_GRP(spi1cs2), \
	NPCM8XX_GRP(spi1cs1), \
	NPCM8XX_GRP(spi1cs0), \
	NPCM8XX_GRP(spi1d23), \
	NPCM8XX_GRP(j2j3), \
	NPCM8XX_GRP(r3oen), \
	NPCM8XX_GRP(r2oen), \
	NPCM8XX_GRP(r1oen), \
	NPCM8XX_GRP(bu4b), \
	NPCM8XX_GRP(bu4), \
	NPCM8XX_GRP(bu5b), \
	NPCM8XX_GRP(bu5), \
	NPCM8XX_GRP(bu6), \
	NPCM8XX_GRP(rmii3), \
	NPCM8XX_GRP(jm1), \
	NPCM8XX_GRP(jm2), \
	NPCM8XX_GRP(tpgpio5b), \
	NPCM8XX_GRP(tpgpio4b), \
	NPCM8XX_GRP(clkrun), \
	NPCM8XX_GRP(i3c5), \
	NPCM8XX_GRP(i3c4), \
	NPCM8XX_GRP(i3c3), \
	NPCM8XX_GRP(i3c2), \
	NPCM8XX_GRP(i3c1), \
	NPCM8XX_GRP(i3c0), \
	NPCM8XX_GRP(hsi1a), \
	NPCM8XX_GRP(hsi2a), \
	NPCM8XX_GRP(hsi1b), \
	NPCM8XX_GRP(hsi2b), \
	NPCM8XX_GRP(hsi1c), \
	NPCM8XX_GRP(hsi2c), \
	NPCM8XX_GRP(smb0), \
	NPCM8XX_GRP(smb0b), \
	NPCM8XX_GRP(smb0c), \
	NPCM8XX_GRP(smb0d), \
	NPCM8XX_GRP(smb0den), \
	NPCM8XX_GRP(smb1), \
	NPCM8XX_GRP(smb1b), \
	NPCM8XX_GRP(smb1c), \
	NPCM8XX_GRP(smb1d), \
	NPCM8XX_GRP(smb2), \
	NPCM8XX_GRP(smb2b), \
	NPCM8XX_GRP(smb2c), \
	NPCM8XX_GRP(smb2d), \
	NPCM8XX_GRP(smb3), \
	NPCM8XX_GRP(smb3b), \
	NPCM8XX_GRP(smb3c), \
	NPCM8XX_GRP(smb3d), \
	NPCM8XX_GRP(smb4), \
	NPCM8XX_GRP(smb4b), \
	NPCM8XX_GRP(smb4c), \
	NPCM8XX_GRP(smb4d), \
	NPCM8XX_GRP(smb5), \
	NPCM8XX_GRP(smb5b), \
	NPCM8XX_GRP(smb5c), \
	NPCM8XX_GRP(smb5d), \
	NPCM8XX_GRP(ga20kbc), \
	NPCM8XX_GRP(smb6), \
	NPCM8XX_GRP(smb6b), \
	NPCM8XX_GRP(smb6c), \
	NPCM8XX_GRP(smb6d), \
	NPCM8XX_GRP(smb7), \
	NPCM8XX_GRP(smb7b), \
	NPCM8XX_GRP(smb7c), \
	NPCM8XX_GRP(smb7d), \
	NPCM8XX_GRP(smb8), \
	NPCM8XX_GRP(smb9), \
	NPCM8XX_GRP(smb10), \
	NPCM8XX_GRP(smb11), \
	NPCM8XX_GRP(smb12), \
	NPCM8XX_GRP(smb13), \
	NPCM8XX_GRP(smb14), \
	NPCM8XX_GRP(smb14b), \
	NPCM8XX_GRP(smb15), \
	NPCM8XX_GRP(smb15b), \
	NPCM8XX_GRP(smb16), \
	NPCM8XX_GRP(smb16b), \
	NPCM8XX_GRP(smb17), \
	NPCM8XX_GRP(smb18), \
	NPCM8XX_GRP(smb19), \
	NPCM8XX_GRP(smb20), \
	NPCM8XX_GRP(smb21), \
	NPCM8XX_GRP(smb22), \
	NPCM8XX_GRP(smb23), \
	NPCM8XX_GRP(smb23b), \
	NPCM8XX_GRP(fanin0), \
	NPCM8XX_GRP(fanin1), \
	NPCM8XX_GRP(fanin2), \
	NPCM8XX_GRP(fanin3), \
	NPCM8XX_GRP(fanin4), \
	NPCM8XX_GRP(fanin5), \
	NPCM8XX_GRP(fanin6), \
	NPCM8XX_GRP(fanin7), \
	NPCM8XX_GRP(fanin8), \
	NPCM8XX_GRP(fanin9), \
	NPCM8XX_GRP(fanin10), \
	NPCM8XX_GRP(fanin11), \
	NPCM8XX_GRP(fanin12), \
	NPCM8XX_GRP(fanin13), \
	NPCM8XX_GRP(fanin14), \
	NPCM8XX_GRP(fanin15), \
	NPCM8XX_GRP(faninx), \
	NPCM8XX_GRP(pwm0), \
	NPCM8XX_GRP(pwm1), \
	NPCM8XX_GRP(pwm2), \
	NPCM8XX_GRP(pwm3), \
	NPCM8XX_GRP(pwm4), \
	NPCM8XX_GRP(pwm5), \
	NPCM8XX_GRP(pwm6), \
	NPCM8XX_GRP(pwm7), \
	NPCM8XX_GRP(pwm8), \
	NPCM8XX_GRP(pwm9), \
	NPCM8XX_GRP(pwm10), \
	NPCM8XX_GRP(pwm11), \
	NPCM8XX_GRP(sg1mdio), \
	NPCM8XX_GRP(rg2), \
	NPCM8XX_GRP(rg2mdio), \
	NPCM8XX_GRP(ddr), \
	NPCM8XX_GRP(uart1), \
	NPCM8XX_GRP(uart2), \
	NPCM8XX_GRP(bmcuart0a), \
	NPCM8XX_GRP(bmcuart0b), \
	NPCM8XX_GRP(bmcuart1), \
	NPCM8XX_GRP(iox1), \
	NPCM8XX_GRP(iox2), \
	NPCM8XX_GRP(ioxh), \
	NPCM8XX_GRP(gspi), \
	NPCM8XX_GRP(mmc), \
	NPCM8XX_GRP(mmcwp), \
	NPCM8XX_GRP(mmccd), \
	NPCM8XX_GRP(mmcrst), \
	NPCM8XX_GRP(mmc8), \
	NPCM8XX_GRP(r1), \
	NPCM8XX_GRP(r1err), \
	NPCM8XX_GRP(r1md), \
	NPCM8XX_GRP(r2), \
	NPCM8XX_GRP(r2err), \
	NPCM8XX_GRP(r2md), \
	NPCM8XX_GRP(sd1), \
	NPCM8XX_GRP(sd1pwr), \
	NPCM8XX_GRP(wdog1), \
	NPCM8XX_GRP(wdog2), \
	NPCM8XX_GRP(scipme), \
	NPCM8XX_GRP(smi), \
	NPCM8XX_GRP(serirq), \
	NPCM8XX_GRP(jtag2), \
	NPCM8XX_GRP(spix), \
	NPCM8XX_GRP(spixcs1), \
	NPCM8XX_GRP(spi1), \
	NPCM8XX_GRP(pspi), \
	NPCM8XX_GRP(ddc), \
	NPCM8XX_GRP(clkreq), \
	NPCM8XX_GRP(clkout), \
	NPCM8XX_GRP(spi3), \
	NPCM8XX_GRP(spi3cs1), \
	NPCM8XX_GRP(spi3quad), \
	NPCM8XX_GRP(spi3cs2), \
	NPCM8XX_GRP(spi3cs3), \
	NPCM8XX_GRP(spi0cs1), \
	NPCM8XX_GRP(lpc), \
	NPCM8XX_GRP(espi), \
	NPCM8XX_GRP(lkgpo0), \
	NPCM8XX_GRP(lkgpo1), \
	NPCM8XX_GRP(lkgpo2), \
	NPCM8XX_GRP(nprd_smi), \
	NPCM8XX_GRP(hgpio0), \
	NPCM8XX_GRP(hgpio1), \
	NPCM8XX_GRP(hgpio2), \
	NPCM8XX_GRP(hgpio3), \
	NPCM8XX_GRP(hgpio4), \
	NPCM8XX_GRP(hgpio5), \
	NPCM8XX_GRP(hgpio6), \
	NPCM8XX_GRP(hgpio7), \
	\

enum {
#define NPCM8XX_GRP(x) fn_ ## x
	NPCM8XX_GRPS
	NPCM8XX_GRP(none),
	NPCM8XX_GRP(gpio),
#undef NPCM8XX_GRP
};

static struct npcm8xx_pingroup npcm8xx_pingroups[] = {
#define NPCM8XX_GRP(x) { .name = #x, .pins = x ## _pins, \
			.npins = ARRAY_SIZE(x ## _pins) }
	NPCM8XX_GRPS
#undef NPCM8XX_GRP
};

#define NPCM8XX_SFUNC(a) NPCM8XX_FUNC(a, #a)
#define NPCM8XX_FUNC(a, b...) static const char *a ## _grp[] = { b }
#define NPCM8XX_MKFUNC(nm) { .name = #nm, .ngroups = ARRAY_SIZE(nm ## _grp), \
			.groups = nm ## _grp }
struct npcm8xx_func {
	const char *name;
	const unsigned int ngroups;
	const char *const *groups;
};

NPCM8XX_SFUNC(gpi36);
NPCM8XX_SFUNC(gpi35);
NPCM8XX_SFUNC(tp_jtag3);
NPCM8XX_SFUNC(tp_uart);
NPCM8XX_SFUNC(tp_smb2);
NPCM8XX_SFUNC(tp_smb1);
NPCM8XX_SFUNC(tp_gpio7);
NPCM8XX_SFUNC(tp_gpio6);
NPCM8XX_SFUNC(tp_gpio5);
NPCM8XX_SFUNC(tp_gpio4);
NPCM8XX_SFUNC(tp_gpio3);
NPCM8XX_SFUNC(tp_gpio2);
NPCM8XX_SFUNC(tp_gpio1);
NPCM8XX_SFUNC(tp_gpio0);
NPCM8XX_SFUNC(tp_gpio2b);
NPCM8XX_SFUNC(tp_gpio1b);
NPCM8XX_SFUNC(tp_gpio0b);
NPCM8XX_SFUNC(vgadig);
NPCM8XX_SFUNC(nbu1crts);
NPCM8XX_SFUNC(fm2);
NPCM8XX_SFUNC(fm1);
NPCM8XX_SFUNC(fm0);
NPCM8XX_SFUNC(gpio1836);
NPCM8XX_SFUNC(gpio1889);
NPCM8XX_SFUNC(gpo187);
NPCM8XX_SFUNC(cp1urxd);
NPCM8XX_SFUNC(r3rxer);
NPCM8XX_SFUNC(cp1gpio2c);
NPCM8XX_SFUNC(cp1gpio3c);
NPCM8XX_SFUNC(cp1gpio0b);
NPCM8XX_SFUNC(cp1gpio1b);
NPCM8XX_SFUNC(cp1gpio2b);
NPCM8XX_SFUNC(cp1gpio3b);
NPCM8XX_SFUNC(cp1gpio4b);
NPCM8XX_SFUNC(cp1gpio5b);
NPCM8XX_SFUNC(cp1gpio6b);
NPCM8XX_SFUNC(cp1gpio7b);
NPCM8XX_SFUNC(cp1gpio0);
NPCM8XX_SFUNC(cp1gpio1);
NPCM8XX_SFUNC(cp1gpio2);
NPCM8XX_SFUNC(cp1gpio3);
NPCM8XX_SFUNC(cp1gpio4);
NPCM8XX_SFUNC(cp1gpio5);
NPCM8XX_SFUNC(cp1gpio6);
NPCM8XX_SFUNC(cp1gpio7);
NPCM8XX_SFUNC(cp1utxd);
NPCM8XX_SFUNC(spi1cs3);
NPCM8XX_SFUNC(spi1cs2);
NPCM8XX_SFUNC(spi1cs1);
NPCM8XX_SFUNC(spi1cs0);
NPCM8XX_SFUNC(spi1d23);
NPCM8XX_SFUNC(j2j3);
NPCM8XX_SFUNC(r3oen);
NPCM8XX_SFUNC(r2oen);
NPCM8XX_SFUNC(r1oen);
NPCM8XX_SFUNC(bu4b);
NPCM8XX_SFUNC(bu4);
NPCM8XX_SFUNC(bu5b);
NPCM8XX_SFUNC(bu5);
NPCM8XX_SFUNC(bu6);
NPCM8XX_SFUNC(rmii3);
NPCM8XX_SFUNC(jm1);
NPCM8XX_SFUNC(jm2);
NPCM8XX_SFUNC(tpgpio5b);
NPCM8XX_SFUNC(tpgpio4b);
NPCM8XX_SFUNC(clkrun);
NPCM8XX_SFUNC(i3c5);
NPCM8XX_SFUNC(i3c4);
NPCM8XX_SFUNC(i3c3);
NPCM8XX_SFUNC(i3c2);
NPCM8XX_SFUNC(i3c1);
NPCM8XX_SFUNC(i3c0);
NPCM8XX_SFUNC(hsi1a);
NPCM8XX_SFUNC(hsi2a);
NPCM8XX_SFUNC(hsi1b);
NPCM8XX_SFUNC(hsi2b);
NPCM8XX_SFUNC(hsi1c);
NPCM8XX_SFUNC(hsi2c);
NPCM8XX_SFUNC(smb0);
NPCM8XX_SFUNC(smb0b);
NPCM8XX_SFUNC(smb0c);
NPCM8XX_SFUNC(smb0d);
NPCM8XX_SFUNC(smb0den);
NPCM8XX_SFUNC(smb1);
NPCM8XX_SFUNC(smb1b);
NPCM8XX_SFUNC(smb1c);
NPCM8XX_SFUNC(smb1d);
NPCM8XX_SFUNC(smb2);
NPCM8XX_SFUNC(smb2b);
NPCM8XX_SFUNC(smb2c);
NPCM8XX_SFUNC(smb2d);
NPCM8XX_SFUNC(smb3);
NPCM8XX_SFUNC(smb3b);
NPCM8XX_SFUNC(smb3c);
NPCM8XX_SFUNC(smb3d);
NPCM8XX_SFUNC(smb4);
NPCM8XX_SFUNC(smb4b);
NPCM8XX_SFUNC(smb4c);
NPCM8XX_SFUNC(smb4d);
NPCM8XX_SFUNC(smb5);
NPCM8XX_SFUNC(smb5b);
NPCM8XX_SFUNC(smb5c);
NPCM8XX_SFUNC(smb5d);
NPCM8XX_SFUNC(ga20kbc);
NPCM8XX_SFUNC(smb6);
NPCM8XX_SFUNC(smb6b);
NPCM8XX_SFUNC(smb6c);
NPCM8XX_SFUNC(smb6d);
NPCM8XX_SFUNC(smb7);
NPCM8XX_SFUNC(smb7b);
NPCM8XX_SFUNC(smb7c);
NPCM8XX_SFUNC(smb7d);
NPCM8XX_SFUNC(smb8);
NPCM8XX_SFUNC(smb9);
NPCM8XX_SFUNC(smb10);
NPCM8XX_SFUNC(smb11);
NPCM8XX_SFUNC(smb12);
NPCM8XX_SFUNC(smb13);
NPCM8XX_SFUNC(smb14);
NPCM8XX_SFUNC(smb14b);
NPCM8XX_SFUNC(smb15);
NPCM8XX_SFUNC(smb16);
NPCM8XX_SFUNC(smb16b);
NPCM8XX_SFUNC(smb17);
NPCM8XX_SFUNC(smb18);
NPCM8XX_SFUNC(smb19);
NPCM8XX_SFUNC(smb20);
NPCM8XX_SFUNC(smb21);
NPCM8XX_SFUNC(smb22);
NPCM8XX_SFUNC(smb23);
NPCM8XX_SFUNC(smb23b);
NPCM8XX_SFUNC(fanin0);
NPCM8XX_SFUNC(fanin1);
NPCM8XX_SFUNC(fanin2);
NPCM8XX_SFUNC(fanin3);
NPCM8XX_SFUNC(fanin4);
NPCM8XX_SFUNC(fanin5);
NPCM8XX_SFUNC(fanin6);
NPCM8XX_SFUNC(fanin7);
NPCM8XX_SFUNC(fanin8);
NPCM8XX_SFUNC(fanin9);
NPCM8XX_SFUNC(fanin10);
NPCM8XX_SFUNC(fanin11);
NPCM8XX_SFUNC(fanin12);
NPCM8XX_SFUNC(fanin13);
NPCM8XX_SFUNC(fanin14);
NPCM8XX_SFUNC(fanin15);
NPCM8XX_SFUNC(faninx);
NPCM8XX_SFUNC(pwm0);
NPCM8XX_SFUNC(pwm1);
NPCM8XX_SFUNC(pwm2);
NPCM8XX_SFUNC(pwm3);
NPCM8XX_SFUNC(pwm4);
NPCM8XX_SFUNC(pwm5);
NPCM8XX_SFUNC(pwm6);
NPCM8XX_SFUNC(pwm7);
NPCM8XX_SFUNC(pwm8);
NPCM8XX_SFUNC(pwm9);
NPCM8XX_SFUNC(pwm10);
NPCM8XX_SFUNC(pwm11);
NPCM8XX_SFUNC(sg1mdio);
NPCM8XX_SFUNC(rg2);
NPCM8XX_SFUNC(rg2mdio);
NPCM8XX_SFUNC(ddr);
NPCM8XX_SFUNC(uart1);
NPCM8XX_SFUNC(uart2);
NPCM8XX_SFUNC(bmcuart0a);
NPCM8XX_SFUNC(bmcuart0b);
NPCM8XX_SFUNC(bmcuart1);
NPCM8XX_SFUNC(iox1);
NPCM8XX_SFUNC(iox2);
NPCM8XX_SFUNC(ioxh);
NPCM8XX_SFUNC(gspi);
NPCM8XX_SFUNC(mmc);
NPCM8XX_SFUNC(mmcwp);
NPCM8XX_SFUNC(mmccd);
NPCM8XX_SFUNC(mmcrst);
NPCM8XX_SFUNC(mmc8);
NPCM8XX_SFUNC(r1);
NPCM8XX_SFUNC(r1err);
NPCM8XX_SFUNC(r1md);
NPCM8XX_SFUNC(r2);
NPCM8XX_SFUNC(r2err);
NPCM8XX_SFUNC(r2md);
NPCM8XX_SFUNC(sd1);
NPCM8XX_SFUNC(sd1pwr);
NPCM8XX_SFUNC(wdog1);
NPCM8XX_SFUNC(wdog2);
NPCM8XX_SFUNC(scipme);
NPCM8XX_SFUNC(smi);
NPCM8XX_SFUNC(serirq);
NPCM8XX_SFUNC(jtag2);
NPCM8XX_SFUNC(spix);
NPCM8XX_SFUNC(spixcs1);
NPCM8XX_SFUNC(spi1);
NPCM8XX_SFUNC(pspi);
NPCM8XX_SFUNC(ddc);
NPCM8XX_SFUNC(clkreq);
NPCM8XX_SFUNC(clkout);
NPCM8XX_SFUNC(spi3);
NPCM8XX_SFUNC(spi3cs1);
NPCM8XX_SFUNC(spi3quad);
NPCM8XX_SFUNC(spi3cs2);
NPCM8XX_SFUNC(spi3cs3);
NPCM8XX_SFUNC(spi0cs1);
NPCM8XX_SFUNC(lpc);
NPCM8XX_SFUNC(espi);
NPCM8XX_SFUNC(lkgpo0);
NPCM8XX_SFUNC(lkgpo1);
NPCM8XX_SFUNC(lkgpo2);
NPCM8XX_SFUNC(nprd_smi);
NPCM8XX_SFUNC(hgpio0);
NPCM8XX_SFUNC(hgpio1);
NPCM8XX_SFUNC(hgpio2);
NPCM8XX_SFUNC(hgpio3);
NPCM8XX_SFUNC(hgpio4);
NPCM8XX_SFUNC(hgpio5);
NPCM8XX_SFUNC(hgpio6);
NPCM8XX_SFUNC(hgpio7);

/* Function names */
static struct npcm8xx_func npcm8xx_funcs[] = {
	NPCM8XX_MKFUNC(gpi36),
	NPCM8XX_MKFUNC(gpi35),
	NPCM8XX_MKFUNC(tp_jtag3),
	NPCM8XX_MKFUNC(tp_uart),
	NPCM8XX_MKFUNC(tp_smb2),
	NPCM8XX_MKFUNC(tp_smb1),
	NPCM8XX_MKFUNC(tp_gpio7),
	NPCM8XX_MKFUNC(tp_gpio6),
	NPCM8XX_MKFUNC(tp_gpio5),
	NPCM8XX_MKFUNC(tp_gpio4),
	NPCM8XX_MKFUNC(tp_gpio3),
	NPCM8XX_MKFUNC(tp_gpio2),
	NPCM8XX_MKFUNC(tp_gpio1),
	NPCM8XX_MKFUNC(tp_gpio0),
	NPCM8XX_MKFUNC(tp_gpio2b),
	NPCM8XX_MKFUNC(tp_gpio1b),
	NPCM8XX_MKFUNC(tp_gpio0b),
	NPCM8XX_MKFUNC(vgadig),
	NPCM8XX_MKFUNC(nbu1crts),
	NPCM8XX_MKFUNC(fm2),
	NPCM8XX_MKFUNC(fm1),
	NPCM8XX_MKFUNC(fm0),
	NPCM8XX_MKFUNC(gpio1836),
	NPCM8XX_MKFUNC(gpio1889),
	NPCM8XX_MKFUNC(gpo187),
	NPCM8XX_MKFUNC(cp1urxd),
	NPCM8XX_MKFUNC(r3rxer),
	NPCM8XX_MKFUNC(cp1gpio2c),
	NPCM8XX_MKFUNC(cp1gpio3c),
	NPCM8XX_MKFUNC(cp1gpio0b),
	NPCM8XX_MKFUNC(cp1gpio1b),
	NPCM8XX_MKFUNC(cp1gpio2b),
	NPCM8XX_MKFUNC(cp1gpio3b),
	NPCM8XX_MKFUNC(cp1gpio4b),
	NPCM8XX_MKFUNC(cp1gpio5b),
	NPCM8XX_MKFUNC(cp1gpio6b),
	NPCM8XX_MKFUNC(cp1gpio7b),
	NPCM8XX_MKFUNC(cp1gpio0),
	NPCM8XX_MKFUNC(cp1gpio1),
	NPCM8XX_MKFUNC(cp1gpio2),
	NPCM8XX_MKFUNC(cp1gpio3),
	NPCM8XX_MKFUNC(cp1gpio4),
	NPCM8XX_MKFUNC(cp1gpio5),
	NPCM8XX_MKFUNC(cp1gpio6),
	NPCM8XX_MKFUNC(cp1gpio7),
	NPCM8XX_MKFUNC(cp1utxd),
	NPCM8XX_MKFUNC(spi1cs3),
	NPCM8XX_MKFUNC(spi1cs2),
	NPCM8XX_MKFUNC(spi1cs1),
	NPCM8XX_MKFUNC(spi1cs0),
	NPCM8XX_MKFUNC(spi1d23),
	NPCM8XX_MKFUNC(j2j3),
	NPCM8XX_MKFUNC(r3oen),
	NPCM8XX_MKFUNC(r2oen),
	NPCM8XX_MKFUNC(r1oen),
	NPCM8XX_MKFUNC(bu4b),
	NPCM8XX_MKFUNC(bu4),
	NPCM8XX_MKFUNC(bu5b),
	NPCM8XX_MKFUNC(bu5),
	NPCM8XX_MKFUNC(bu6),
	NPCM8XX_MKFUNC(rmii3),
	NPCM8XX_MKFUNC(jm1),
	NPCM8XX_MKFUNC(jm2),
	NPCM8XX_MKFUNC(tpgpio5b),
	NPCM8XX_MKFUNC(tpgpio4b),
	NPCM8XX_MKFUNC(clkrun),
	NPCM8XX_MKFUNC(i3c5),
	NPCM8XX_MKFUNC(i3c4),
	NPCM8XX_MKFUNC(i3c3),
	NPCM8XX_MKFUNC(i3c2),
	NPCM8XX_MKFUNC(i3c1),
	NPCM8XX_MKFUNC(i3c0),
	NPCM8XX_MKFUNC(hsi1a),
	NPCM8XX_MKFUNC(hsi2a),
	NPCM8XX_MKFUNC(hsi1b),
	NPCM8XX_MKFUNC(hsi2b),
	NPCM8XX_MKFUNC(hsi1c),
	NPCM8XX_MKFUNC(hsi2c),
	NPCM8XX_MKFUNC(smb0),
	NPCM8XX_MKFUNC(smb0b),
	NPCM8XX_MKFUNC(smb0c),
	NPCM8XX_MKFUNC(smb0d),
	NPCM8XX_MKFUNC(smb0den),
	NPCM8XX_MKFUNC(smb1),
	NPCM8XX_MKFUNC(smb1b),
	NPCM8XX_MKFUNC(smb1c),
	NPCM8XX_MKFUNC(smb1d),
	NPCM8XX_MKFUNC(smb2),
	NPCM8XX_MKFUNC(smb2b),
	NPCM8XX_MKFUNC(smb2c),
	NPCM8XX_MKFUNC(smb2d),
	NPCM8XX_MKFUNC(smb3),
	NPCM8XX_MKFUNC(smb3b),
	NPCM8XX_MKFUNC(smb3c),
	NPCM8XX_MKFUNC(smb3d),
	NPCM8XX_MKFUNC(smb4),
	NPCM8XX_MKFUNC(smb4b),
	NPCM8XX_MKFUNC(smb4c),
	NPCM8XX_MKFUNC(smb4d),
	NPCM8XX_MKFUNC(smb5),
	NPCM8XX_MKFUNC(smb5b),
	NPCM8XX_MKFUNC(smb5c),
	NPCM8XX_MKFUNC(smb5d),
	NPCM8XX_MKFUNC(ga20kbc),
	NPCM8XX_MKFUNC(smb6),
	NPCM8XX_MKFUNC(smb6b),
	NPCM8XX_MKFUNC(smb6c),
	NPCM8XX_MKFUNC(smb6d),
	NPCM8XX_MKFUNC(smb7),
	NPCM8XX_MKFUNC(smb7b),
	NPCM8XX_MKFUNC(smb7c),
	NPCM8XX_MKFUNC(smb7d),
	NPCM8XX_MKFUNC(smb8),
	NPCM8XX_MKFUNC(smb9),
	NPCM8XX_MKFUNC(smb10),
	NPCM8XX_MKFUNC(smb11),
	NPCM8XX_MKFUNC(smb12),
	NPCM8XX_MKFUNC(smb13),
	NPCM8XX_MKFUNC(smb14),
	NPCM8XX_MKFUNC(smb14b),
	NPCM8XX_MKFUNC(smb15),
	NPCM8XX_MKFUNC(smb16),
	NPCM8XX_MKFUNC(smb16b),
	NPCM8XX_MKFUNC(smb17),
	NPCM8XX_MKFUNC(smb18),
	NPCM8XX_MKFUNC(smb19),
	NPCM8XX_MKFUNC(smb20),
	NPCM8XX_MKFUNC(smb21),
	NPCM8XX_MKFUNC(smb22),
	NPCM8XX_MKFUNC(smb23),
	NPCM8XX_MKFUNC(smb23b),
	NPCM8XX_MKFUNC(fanin0),
	NPCM8XX_MKFUNC(fanin1),
	NPCM8XX_MKFUNC(fanin2),
	NPCM8XX_MKFUNC(fanin3),
	NPCM8XX_MKFUNC(fanin4),
	NPCM8XX_MKFUNC(fanin5),
	NPCM8XX_MKFUNC(fanin6),
	NPCM8XX_MKFUNC(fanin7),
	NPCM8XX_MKFUNC(fanin8),
	NPCM8XX_MKFUNC(fanin9),
	NPCM8XX_MKFUNC(fanin10),
	NPCM8XX_MKFUNC(fanin11),
	NPCM8XX_MKFUNC(fanin12),
	NPCM8XX_MKFUNC(fanin13),
	NPCM8XX_MKFUNC(fanin14),
	NPCM8XX_MKFUNC(fanin15),
	NPCM8XX_MKFUNC(faninx),
	NPCM8XX_MKFUNC(pwm0),
	NPCM8XX_MKFUNC(pwm1),
	NPCM8XX_MKFUNC(pwm2),
	NPCM8XX_MKFUNC(pwm3),
	NPCM8XX_MKFUNC(pwm4),
	NPCM8XX_MKFUNC(pwm5),
	NPCM8XX_MKFUNC(pwm6),
	NPCM8XX_MKFUNC(pwm7),
	NPCM8XX_MKFUNC(pwm8),
	NPCM8XX_MKFUNC(pwm9),
	NPCM8XX_MKFUNC(pwm10),
	NPCM8XX_MKFUNC(pwm11),
	NPCM8XX_MKFUNC(sg1mdio),
	NPCM8XX_MKFUNC(rg2),
	NPCM8XX_MKFUNC(rg2mdio),
	NPCM8XX_MKFUNC(ddr),
	NPCM8XX_MKFUNC(uart1),
	NPCM8XX_MKFUNC(uart2),
	NPCM8XX_MKFUNC(bmcuart0a),
	NPCM8XX_MKFUNC(bmcuart0b),
	NPCM8XX_MKFUNC(bmcuart1),
	NPCM8XX_MKFUNC(iox1),
	NPCM8XX_MKFUNC(iox2),
	NPCM8XX_MKFUNC(ioxh),
	NPCM8XX_MKFUNC(gspi),
	NPCM8XX_MKFUNC(mmc),
	NPCM8XX_MKFUNC(mmcwp),
	NPCM8XX_MKFUNC(mmccd),
	NPCM8XX_MKFUNC(mmcrst),
	NPCM8XX_MKFUNC(mmc8),
	NPCM8XX_MKFUNC(r1),
	NPCM8XX_MKFUNC(r1err),
	NPCM8XX_MKFUNC(r1md),
	NPCM8XX_MKFUNC(r2),
	NPCM8XX_MKFUNC(r2err),
	NPCM8XX_MKFUNC(r2md),
	NPCM8XX_MKFUNC(sd1),
	NPCM8XX_MKFUNC(sd1pwr),
	NPCM8XX_MKFUNC(wdog1),
	NPCM8XX_MKFUNC(wdog2),
	NPCM8XX_MKFUNC(scipme),
	NPCM8XX_MKFUNC(smi),
	NPCM8XX_MKFUNC(serirq),
	NPCM8XX_MKFUNC(jtag2),
	NPCM8XX_MKFUNC(spix),
	NPCM8XX_MKFUNC(spixcs1),
	NPCM8XX_MKFUNC(spi1),
	NPCM8XX_MKFUNC(pspi),
	NPCM8XX_MKFUNC(ddc),
	NPCM8XX_MKFUNC(clkreq),
	NPCM8XX_MKFUNC(clkout),
	NPCM8XX_MKFUNC(spi3),
	NPCM8XX_MKFUNC(spi3cs1),
	NPCM8XX_MKFUNC(spi3quad),
	NPCM8XX_MKFUNC(spi3cs2),
	NPCM8XX_MKFUNC(spi3cs3),
	NPCM8XX_MKFUNC(spi0cs1),
	NPCM8XX_MKFUNC(lpc),
	NPCM8XX_MKFUNC(espi),
	NPCM8XX_MKFUNC(lkgpo0),
	NPCM8XX_MKFUNC(lkgpo1),
	NPCM8XX_MKFUNC(lkgpo2),
	NPCM8XX_MKFUNC(nprd_smi),
	NPCM8XX_MKFUNC(hgpio0),
	NPCM8XX_MKFUNC(hgpio1),
	NPCM8XX_MKFUNC(hgpio2),
	NPCM8XX_MKFUNC(hgpio3),
	NPCM8XX_MKFUNC(hgpio4),
	NPCM8XX_MKFUNC(hgpio5),
	NPCM8XX_MKFUNC(hgpio6),
	NPCM8XX_MKFUNC(hgpio7),
};

#define NPCM8XX_PINCFG(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q) \
	[a] { .fn0 = fn_ ## b, .reg0 = NPCM8XX_GCR_ ## c, .bit0 = d, \
			.fn1 = fn_ ## e, .reg1 = NPCM8XX_GCR_ ## f, .bit1 = g, \
			.fn2 = fn_ ## h, .reg2 = NPCM8XX_GCR_ ## i, .bit2 = j, \
			.fn3 = fn_ ## k, .reg3 = NPCM8XX_GCR_ ## l, .bit3 = m, \
			.fn4 = fn_ ## n, .reg4 = NPCM8XX_GCR_ ## o, .bit4 = p, \
			.flag = q }

/* Drive strength controlled by NPCM8XX_GP_N_ODSC */
#define DRIVE_STRENGTH_LO_SHIFT		8
#define DRIVE_STRENGTH_HI_SHIFT		12
#define DRIVE_STRENGTH_MASK		GENMASK(15, 8)

#define DSTR(lo, hi)	(((lo) << DRIVE_STRENGTH_LO_SHIFT) | \
			 ((hi) << DRIVE_STRENGTH_HI_SHIFT))
#define DSLO(x)		(((x) >> DRIVE_STRENGTH_LO_SHIFT) & GENMASK(3, 0))
#define DSHI(x)		(((x) >> DRIVE_STRENGTH_HI_SHIFT) & GENMASK(3, 0))

#define GPI		BIT(0) /* Not GPO */
#define GPO		BIT(1) /* Not GPI */
#define SLEW		BIT(2) /* Has Slew Control, NPCM8XX_GP_N_OSRC */
#define SLEWLPC		BIT(3) /* Has Slew Control, SRCNT.3 */

struct npcm8xx_pincfg {
	int flag;
	int fn0, reg0, bit0;
	int fn1, reg1, bit1;
	int fn2, reg2, bit2;
	int fn3, reg3, bit3;
	int fn4, reg4, bit4;
};

static const struct npcm8xx_pincfg pincfg[] = {
	/*		PIN	  FUNCTION 1		   FUNCTION 2		  FUNCTION 3		FUNCTION 4		FUNCTION 5		FLAGS */
	NPCM8XX_PINCFG(0,	iox1, MFSEL1, 30,	smb6c, I2CSEGSEL, 25,	smb18, MFSEL5, 26,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(1,	iox1, MFSEL1, 30,	smb6c, I2CSEGSEL, 25,	smb18, MFSEL5, 26,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(2,	iox1, MFSEL1, 30,	smb6b, I2CSEGSEL, 24,	smb17, MFSEL5, 25,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(3,	iox1, MFSEL1, 30,	smb6b, I2CSEGSEL, 24,	smb17, MFSEL5, 25,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(4,	iox2, MFSEL3, 14,	smb1d, I2CSEGSEL, 7,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(5,	iox2, MFSEL3, 14,	smb1d, I2CSEGSEL, 7,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(6,	iox2, MFSEL3, 14,	smb2d, I2CSEGSEL, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(7,	iox2, MFSEL3, 14,	smb2d, I2CSEGSEL, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(8,	lkgpo1,	FLOCKR1, 4,	tp_gpio0b, MFSEL7, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(9,	lkgpo2,	FLOCKR1, 8,	tp_gpio1b, MFSEL7, 9,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(10,	ioxh, MFSEL3, 18,	smb6d, I2CSEGSEL, 26,	smb16, MFSEL5, 24,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(11,	ioxh, MFSEL3, 18,	smb6d, I2CSEGSEL, 26,	smb16, MFSEL5, 24,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(12,	gspi, MFSEL1, 24,	smb5b, I2CSEGSEL, 19,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(13,	gspi, MFSEL1, 24,	smb5b, I2CSEGSEL, 19,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(14,	gspi, MFSEL1, 24,	smb5c, I2CSEGSEL, 20,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(15,	gspi, MFSEL1, 24,	smb5c, I2CSEGSEL, 20,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(16,	lkgpo0, FLOCKR1, 0,	smb7b, I2CSEGSEL, 27,	tp_gpio2b, MFSEL7, 10,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(17,	pspi, MFSEL3, 13,	cp1gpio5, MFSEL6, 7,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(18,	pspi, MFSEL3, 13,	smb4b, I2CSEGSEL, 14,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(19,	pspi, MFSEL3, 13,	smb4b, I2CSEGSEL, 14,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(20,	hgpio0,	MFSEL2, 24,	smb15, MFSEL3, 8,	smb4c, I2CSEGSEL, 15,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(21,	hgpio1,	MFSEL2, 25,	smb15, MFSEL3, 8,	smb4c, I2CSEGSEL, 15,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(22,	hgpio2,	MFSEL2, 26,	smb14, MFSEL3, 7,	smb4d, I2CSEGSEL, 16,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(23,	hgpio3,	MFSEL2, 27,	smb14, MFSEL3, 7,	smb4d, I2CSEGSEL, 16,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(24,	hgpio4,	MFSEL2, 28,	ioxh, MFSEL3, 18,	smb7c, I2CSEGSEL, 28,	tp_smb2, MFSEL7, 28,	none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(25,	hgpio5,	MFSEL2, 29,	ioxh, MFSEL3, 18,	smb7c, I2CSEGSEL, 28,	tp_smb2, MFSEL7, 28,	none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(26,	smb5, MFSEL1, 2,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(27,	smb5, MFSEL1, 2,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(28,	smb4, MFSEL1, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(29,	smb4, MFSEL1, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(30,	smb3, MFSEL1, 0,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(31,	smb3, MFSEL1, 0,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(32,	spi0cs1, MFSEL1, 3,	smb14b, MFSEL7, 26,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(33,	i3c4, MFSEL6, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(34,	i3c4, MFSEL6, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(35,	gpi35, MFSEL5, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(36,	gpi36, MFSEL5, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(37,	smb3c, I2CSEGSEL, 12,	smb23, MFSEL5, 31,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(38,	smb3c, I2CSEGSEL, 12,	smb23, MFSEL5, 31,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(39,	smb3b, I2CSEGSEL, 11,	smb22, MFSEL5, 30,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(40,	smb3b, I2CSEGSEL, 11,	smb22, MFSEL5, 30,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(41,	bmcuart0a, MFSEL1, 9,	cp1urxd, MFSEL6, 31,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(42,	bmcuart0a, MFSEL1, 9,	cp1utxd, MFSEL6, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(2, 4) | GPO),
	NPCM8XX_PINCFG(43,	uart1, MFSEL1, 10,	bmcuart1, MFSEL3, 24,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(44,	hsi1b, MFSEL1, 28,	nbu1crts, MFSEL6, 15,	jtag2, MFSEL4, 0,	tp_jtag3, MFSEL7, 13,	j2j3, MFSEL5, 2,	GPO),
	NPCM8XX_PINCFG(45,	hsi1c, MFSEL1, 4,	jtag2, MFSEL4, 0,	j2j3, MFSEL5, 2,	tp_jtag3, MFSEL7, 13,	none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(46,	hsi1c, MFSEL1, 4,	jtag2, MFSEL4, 0,	j2j3, MFSEL5, 2,	tp_jtag3, MFSEL7, 13,	none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(47,	hsi1c, MFSEL1, 4,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(2, 8)),
	NPCM8XX_PINCFG(48,	hsi2a, MFSEL1, 11,	bmcuart0b, MFSEL4, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(49,	hsi2a, MFSEL1, 11,	bmcuart0b, MFSEL4, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(50,	hsi2b, MFSEL1, 29,	bu6, MFSEL5, 6,		tp_uart, MFSEL7, 12,	none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(51,	hsi2b, MFSEL1, 29,	bu6, MFSEL5, 6,		tp_uart, MFSEL7, 12,	none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(52,	hsi2c, MFSEL1, 5,	bu5, MFSEL5, 7,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(53,	hsi2c, MFSEL1, 5,	bu5, MFSEL5, 7,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(54,	hsi2c, MFSEL1, 5,	bu4, MFSEL5, 8,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(55,	hsi2c, MFSEL1, 5,	bu4, MFSEL5, 8,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(56,	r1err, MFSEL1, 12,	r1oen, MFSEL5, 9,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(57,	r1md, MFSEL1, 13,	tpgpio4b, MFSEL5, 20,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(2, 4)),
	NPCM8XX_PINCFG(58,	r1md, MFSEL1, 13,	tpgpio5b, MFSEL5, 22,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(2, 4)),
	NPCM8XX_PINCFG(59,	hgpio6, MFSEL2, 30,	smb3d, I2CSEGSEL, 13,	smb19, MFSEL5, 27,	none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(60,	hgpio7, MFSEL2, 31,	smb3d, I2CSEGSEL, 13,	smb19, MFSEL5, 27,	none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(61,	hsi1c, MFSEL1, 4,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(62,	hsi1b, MFSEL1, 28,	jtag2, MFSEL4, 0,	j2j3, MFSEL5, 2,	nbu1crts, MFSEL6, 15,	tp_jtag3, MFSEL7, 13,	GPO),
	NPCM8XX_PINCFG(63,	hsi1a, MFSEL1, 10,	bmcuart1, MFSEL3, 24,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(64,	fanin0, MFSEL2, 0,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(65,	fanin1, MFSEL2, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(66,	fanin2, MFSEL2, 2,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(67,	fanin3, MFSEL2, 3,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(68,	fanin4, MFSEL2, 4,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(69,	fanin5, MFSEL2, 5,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(70,	fanin6, MFSEL2, 6,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(71,	fanin7, MFSEL2, 7,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(72,	fanin8, MFSEL2, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(73,	fanin9, MFSEL2, 9,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(74,	fanin10, MFSEL2, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(75,	fanin11, MFSEL2, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(76,	fanin12, MFSEL2, 12,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(77,	fanin13, MFSEL2, 13,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(78,	fanin14, MFSEL2, 14,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(79,	fanin15, MFSEL2, 15,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(80,	pwm0, MFSEL2, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(81,	pwm1, MFSEL2, 17,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(82,	pwm2, MFSEL2, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(83,	pwm3, MFSEL2, 19,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(84,	r2, MFSEL1, 14,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(85,	r2, MFSEL1, 14,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(86,	r2, MFSEL1, 14,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(87,	r2, MFSEL1, 14,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(88,	r2, MFSEL1, 14,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(89,	r2, MFSEL1, 14,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(90,	r2err, MFSEL1, 15,	r2oen, MFSEL5, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(91,	r2md, MFSEL1, 16,	cp1gpio6, MFSEL6, 8,	tp_gpio0, MFSEL7, 0,	none, NONE, 0,		none, NONE, 0,		DSTR(2, 4)),
	NPCM8XX_PINCFG(92,	r2md, MFSEL1, 16,	cp1gpio7, MFSEL6, 9,	tp_gpio1, MFSEL7, 1,	none, NONE, 0,		none, NONE, 0,		DSTR(2, 4)),
	NPCM8XX_PINCFG(93,	ga20kbc, MFSEL1, 17,	smb5d, I2CSEGSEL, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(94,	ga20kbc, MFSEL1, 17,	smb5d, I2CSEGSEL, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(95,	lpc, MFSEL1, 26,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(96,	cp1gpio7b, MFSEL6, 24,	tp_gpio7, MFSEL7, 7,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(97,	cp1gpio6b, MFSEL6, 25,	tp_gpio6, MFSEL7, 6,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(98,	bu4b, MFSEL5, 13,	cp1gpio5b, MFSEL6, 26,	tp_gpio5, MFSEL7, 5,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(99,	bu4b, MFSEL5, 13,	cp1gpio4b, MFSEL6, 27,	tp_gpio4, MFSEL7, 4,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(100,	bu5b, MFSEL5, 12,	cp1gpio3c, MFSEL6, 28,	tp_gpio3, MFSEL7, 3,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(101,	bu5b, MFSEL5, 12,	cp1gpio2c, MFSEL6, 29,	tp_gpio2, MFSEL7, 2,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(102,	vgadig, MFSEL7, 29,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(103,	vgadig, MFSEL7, 29,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(104,	vgadig, MFSEL7, 29,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(105,	vgadig, MFSEL7, 29,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(106,	i3c5, MFSEL3, 22,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(107,	i3c5, MFSEL3, 22,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(108,	sg1mdio, MFSEL4, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(109,	sg1mdio, MFSEL4, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(110,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	rmii3, MFSEL5, 11,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(111,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	rmii3, MFSEL5, 11,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(112,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(113,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(114,	smb0, MFSEL1, 6,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(115,	smb0, MFSEL1, 6,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(116,	smb1, MFSEL1, 7,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(117,	smb1, MFSEL1, 7,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(118,	smb2, MFSEL1, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(119,	smb2, MFSEL1, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(120,	smb2c, I2CSEGSEL, 9,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(121,	smb2c, I2CSEGSEL, 9,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(122,	smb2b, I2CSEGSEL, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(123,	smb2b, I2CSEGSEL, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(124,	smb1c, I2CSEGSEL, 6,	cp1gpio3b, MFSEL6, 23,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(125,	smb1c, I2CSEGSEL, 6,	cp1gpio2b, MFSEL6, 22,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(126,	smb1b, I2CSEGSEL, 5,	cp1gpio1b, MFSEL6, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(127,	smb1b, I2CSEGSEL, 5,	cp1gpio0b, MFSEL6, 20,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(128,	smb8, MFSEL4, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(129,	smb8, MFSEL4, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(130,	smb9, MFSEL4, 12,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(131,	smb9, MFSEL4, 12,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(132,	smb10, MFSEL4, 13,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(133,	smb10, MFSEL4, 13,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(134,	smb11, MFSEL4, 14,	smb23b, MFSEL6, 0,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(135,	smb11, MFSEL4, 14,	smb23b, MFSEL6, 0,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(136,	jm1, MFSEL5, 15,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(137,	jm1, MFSEL5, 15,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(138,	jm1, MFSEL5, 15,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(139,	jm1, MFSEL5, 15,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(140,	jm1, MFSEL5, 15,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(141,	smb7b, I2CSEGSEL, 27,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(142,	smb7d, I2CSEGSEL, 29,	tp_smb1, MFSEL7, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(143,	smb7d, I2CSEGSEL, 29,	tp_smb1, MFSEL7, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(144,	pwm4, MFSEL2, 20,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(145,	pwm5, MFSEL2, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(146,	pwm6, MFSEL2, 22,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(147,	pwm7, MFSEL2, 23,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 8)),
	NPCM8XX_PINCFG(148,	mmc8, MFSEL3, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(149,	mmc8, MFSEL3, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(150,	mmc8, MFSEL3, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(151,	mmc8, MFSEL3, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(152,	mmc, MFSEL3, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(153,	mmcwp, FLOCKR1, 24,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(154,	mmc, MFSEL3, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(155,	mmccd, MFSEL3, 25,	mmcrst, MFSEL4, 6,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(156,	mmc, MFSEL3, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(157,	mmc, MFSEL3, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(158,	mmc, MFSEL3, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(159,	mmc, MFSEL3, 10,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(160,	clkout, MFSEL1, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(161,	lpc, MFSEL1, 26,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(162,	clkrun, MFSEL3, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(163,	lpc, MFSEL1, 26,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(164,	lpc, MFSEL1, 26,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(165,	lpc, MFSEL1, 26,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(166,	lpc, MFSEL1, 26,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(167,	lpc, MFSEL1, 26,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(168,	serirq, MFSEL1, 31,	espi, MFSEL4, 8,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(169,	scipme, MFSEL3, 0,	smb21, MFSEL5, 29,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(170,	smi, MFSEL1, 22,	smb21, MFSEL5, 29,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(171,	smb6, MFSEL3, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(172,	smb6, MFSEL3, 1,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(173,	smb7, MFSEL3, 2,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(174,	smb7, MFSEL3, 2,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(175,	spi1, MFSEL3, 4,	faninx, MFSEL3, 3,	fm1, MFSEL6, 17,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(176,	spi1, MFSEL3, 4,	faninx, MFSEL3, 3,	fm1, MFSEL6, 17,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(177,	spi1, MFSEL3, 4,	faninx, MFSEL3, 3,	fm1, MFSEL6, 17,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(178,	r1, MFSEL3, 9,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(179,	r1, MFSEL3, 9,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(180,	r1, MFSEL3, 9,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(181,	r1, MFSEL3, 9,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(182,	r1, MFSEL3, 9,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(183,	gpio1836, MFSEL6, 19,	spi3, MFSEL4, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(184,	gpio1836, MFSEL6, 19,	spi3, MFSEL4, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(185,	gpio1836, MFSEL6, 19,	spi3, MFSEL4, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(186,	gpio1836, MFSEL6, 19,	spi3, MFSEL4, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(187,	gpo187, MFSEL7, 24,	smb14b, MFSEL7, 26,	spi3cs1, MFSEL4, 17,	none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(188,	gpio1889, MFSEL7, 25,	spi3cs2, MFSEL4, 18,	spi3quad, MFSEL4, 20,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(189,	gpio1889, MFSEL7, 25,	spi3cs3, MFSEL4, 19,	spi3quad, MFSEL4, 20,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(190,	nprd_smi, FLOCKR1, 20,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(2, 4)),
	NPCM8XX_PINCFG(191,	spi1d23, MFSEL5, 3,	spi1cs2, MFSEL5, 4,	fm1, MFSEL6, 17,	smb15, MFSEL7, 27,	none, NONE, 0,		SLEW),  /* XX */
	NPCM8XX_PINCFG(192,	spi1d23, MFSEL5, 3,	spi1cs3, MFSEL5, 5,	fm1, MFSEL6, 17,	smb15, MFSEL7, 27,	none, NONE, 0,		SLEW),  /* XX */
	NPCM8XX_PINCFG(193,	r1, MFSEL3, 9,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(194,	smb0b, I2CSEGSEL, 0,	fm0, MFSEL6, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(195,	smb0b, I2CSEGSEL, 0,	fm0, MFSEL6, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(196,	smb0c, I2CSEGSEL, 1,	fm0, MFSEL6, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(197,	smb0den, I2CSEGSEL, 22,	fm0, MFSEL6, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(198,	smb0d, I2CSEGSEL, 2,	fm0, MFSEL6, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(199,	smb0d, I2CSEGSEL, 2,	fm0, MFSEL6, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(200,	r2, MFSEL1, 14,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(201,	r1, MFSEL3, 9,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO),
	NPCM8XX_PINCFG(202,	smb0c, I2CSEGSEL, 1,	fm0, MFSEL6, 16,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(203,	faninx, MFSEL3, 3,	spi1cs0, MFSEL3, 4,	fm1, MFSEL6, 17,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12)),
	NPCM8XX_PINCFG(208,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW), /* DSCNT */
	NPCM8XX_PINCFG(209,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	rmii3, MFSEL5, 11,	none, NONE, 0,		none, NONE, 0,		SLEW), /* DSCNT */
	NPCM8XX_PINCFG(210,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	rmii3, MFSEL5, 11,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(211,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	rmii3, MFSEL5, 11,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(212,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	r3rxer, MFSEL6, 30,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(213,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	r3oen, MFSEL5, 14,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(214,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	rmii3, MFSEL5, 11,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(215,	rg2, MFSEL4, 24,	ddr, MFSEL3, 26,	rmii3, MFSEL5, 11,	none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(216,	rg2mdio, MFSEL4, 23,	ddr, MFSEL3, 26,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(217,	rg2mdio, MFSEL4, 23,	ddr, MFSEL3, 26,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(218,	wdog1, MFSEL3, 19,	smb16b, MFSEL7, 30,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(219,	wdog2, MFSEL3, 20,	smb16b, MFSEL7, 30,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(220,	smb12, MFSEL3, 5,	pwm8, MFSEL6, 11,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(221,	smb12, MFSEL3, 5,	pwm9, MFSEL6, 12,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(222,	smb13, MFSEL3, 6,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(223,	smb13, MFSEL3, 6,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(224,	spix, MFSEL4, 27,	fm2, MFSEL6, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(225,	spix, MFSEL4, 27,	fm2, MFSEL6, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(226,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO | DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(227,	spix, MFSEL4, 27,	fm2, MFSEL6, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(228,	spixcs1, MFSEL4, 28,	fm2, MFSEL6, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(229,	spix, MFSEL4, 27,	fm2, MFSEL6, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO | DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(230,	spix, MFSEL4, 27,	fm2, MFSEL6, 18,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPO | DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(231,	clkreq, MFSEL4, 9,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(4, 12) | SLEW),
	NPCM8XX_PINCFG(233,	spi1cs1, MFSEL5, 0,	fm1, MFSEL6, 17,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0), /* slewlpc ? */
	NPCM8XX_PINCFG(234,	pwm10, MFSEL6, 13,	smb20, MFSEL5, 28,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(235,	pwm11, MFSEL6, 14,	smb20, MFSEL5, 28,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(240,	i3c0, MFSEL5, 17,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(241,	i3c0, MFSEL5, 17,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(242,	i3c1, MFSEL5, 19,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(243,	i3c1, MFSEL5, 19,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(244,	i3c2, MFSEL5, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(245,	i3c2, MFSEL5, 21,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(246,	i3c3, MFSEL5, 23,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(247,	i3c3, MFSEL5, 23,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		SLEW),
	NPCM8XX_PINCFG(250,	ddr, MFSEL3, 26,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		DSTR(8, 12) | SLEW),
	NPCM8XX_PINCFG(251,	jm2, MFSEL5, 1,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		0),
	NPCM8XX_PINCFG(253,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPI), /* SDHC1 power */
	NPCM8XX_PINCFG(254,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPI), /* SDHC2 power */
	NPCM8XX_PINCFG(255,	none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		none, NONE, 0,		GPI), /* DACOSEL */
};

/* number, name, drv_data */
static const struct pinctrl_pin_desc npcm8xx_pins[] = {
	PINCTRL_PIN(0,	"GPIO0/IOX1_DI/SMB6C_SDA/SMB18_SDA"),
	PINCTRL_PIN(1,	"GPIO1/IOX1_LD/SMB6C_SCL/SMB18_SCL"),
	PINCTRL_PIN(2,	"GPIO2/IOX1_CK/SMB6B_SDA/SMB17_SDA"),
	PINCTRL_PIN(3,	"GPIO3/IOX1_DO/SMB6B_SCL/SMB17_SCL"),
	PINCTRL_PIN(4,	"GPIO4/IOX2_DI/SMB1D_SDA"),
	PINCTRL_PIN(5,	"GPIO5/IOX2_LD/SMB1D_SCL"),
	PINCTRL_PIN(6,	"GPIO6/IOX2_CK/SMB2D_SDA"),
	PINCTRL_PIN(7,	"GPIO7/IOX2_D0/SMB2D_SCL"),
	PINCTRL_PIN(8,	"GPIO8/LKGPO1/TP_GPIO0"),
	PINCTRL_PIN(9,	"GPIO9/LKGPO2/TP_GPIO1"),
	PINCTRL_PIN(10, "GPIO10/IOXH_LD/SMB6D_SCL/SMB16_SCL"),
	PINCTRL_PIN(11, "GPIO11/IOXH_CK/SMB6D_SDA/SMB16_SDA"),
	PINCTRL_PIN(12, "GPIO12/GSPI_CK/SMB5B_SCL"),
	PINCTRL_PIN(13, "GPIO13/GSPI_DO/SMB5B_SDA"),
	PINCTRL_PIN(14, "GPIO14/GSPI_DI/SMB5C_SCL"),
	PINCTRL_PIN(15, "GPIO15/GSPI_CS/SMB5C_SDA"),
	PINCTRL_PIN(16, "GPIO16/SMB7B_SDA/LKGPO0/TP_GPIO2"),
	PINCTRL_PIN(17, "GPIO17/PSPI_DI/CP1_GPIO5"),
	PINCTRL_PIN(18, "GPIO18/PSPI_D0/SMB4B_SDA"),
	PINCTRL_PIN(19, "GPIO19/PSPI_CK/SMB4B_SCL"),
	PINCTRL_PIN(20, "GPIO20/H_GPIO0/SMB4C_SDA/SMB15_SDA"),
	PINCTRL_PIN(21, "GPIO21/H_GPIO1/SMB4C_SCL/SMB15_SCL"),
	PINCTRL_PIN(22, "GPIO22/H_GPIO2/SMB4D_SDA/SMB14_SDA"),
	PINCTRL_PIN(23, "GPIO23/H_GPIO3/SMB4D_SCL/SMB14_SCL"),
	PINCTRL_PIN(24, "GPIO24/IOXH_DO/H_GPIO4/SMB7C_SCL/TP_SMB2_SCL"),
	PINCTRL_PIN(25, "GPIO25/IOXH_DI/H_GPIO4/SMB7C_SDA/TP_SMB2_SDA"),
	PINCTRL_PIN(26, "GPIO26/SMB5_SDA"),
	PINCTRL_PIN(27, "GPIO27/SMB5_SCL"),
	PINCTRL_PIN(28, "GPIO28/SMB4_SDA"),
	PINCTRL_PIN(29, "GPIO29/SMB4_SCL"),
	PINCTRL_PIN(30, "GPIO30/SMB3_SDA"),
	PINCTRL_PIN(31, "GPIO31/SMB3_SCL"),
	PINCTRL_PIN(32, "GPIO32/SMB14B_SCL/SPI0_nCS1"),
	PINCTRL_PIN(33, "GPIO33/I3C4_SCL"),
	PINCTRL_PIN(34, "GPIO34/I3C4_SDA"),
	PINCTRL_PIN(35, "MCBPCK/GPI35_AHB2PCI_DIS"),
	PINCTRL_PIN(36, "SYSBPCK/GPI36"),
	PINCTRL_PIN(37, "GPIO37/SMB3C_SDA/SMB23_SDA"),
	PINCTRL_PIN(38, "GPIO38/SMB3C_SCL/SMB23_SCL"),
	PINCTRL_PIN(39, "GPIO39/SMB3B_SDA/SMB22_SDA"),
	PINCTRL_PIN(40, "GPIO40/SMB3B_SCL/SMB22_SCL"),
	PINCTRL_PIN(41, "GPIO41/BU0_RXD/CP1U_RXD"),
	PINCTRL_PIN(42, "GPIO42/BU0_TXD/CP1U_TXD"),
	PINCTRL_PIN(43, "GPIO43/SI1_RXD/BU1_RXD"),
	PINCTRL_PIN(44, "GPIO44/SI1_nCTS/BU1_nCTS/CP_TDI/TP_TDI/CP_TP_TDI"),
	PINCTRL_PIN(45, "GPIO45/SI1_nDCD/CP_TMS_SWIO/TP_TMS_SWIO/CP_TP_TMS_SWIO"),
	PINCTRL_PIN(46, "GPIO46/SI1_nDSR/CP_TCK_SWCLK/TP_TCK_SWCLK/CP_TP_TCK_SWCLK"),
	PINCTRL_PIN(47, "GPIO47/SI1n_RI1"),
	PINCTRL_PIN(48, "GPIO48/SI2_TXD/BU0_TXD/STRAP5"),
	PINCTRL_PIN(49, "GPIO49/SI2_RXD/BU0_RXD"),
	PINCTRL_PIN(50, "GPIO50/SI2_nCTS/BU6_TXD/TPU_TXD"),
	PINCTRL_PIN(51, "GPIO51/SI2_nRTS/BU6_RXD/TPU_RXD"),
	PINCTRL_PIN(52, "GPIO52/SI2_nDCD/BU5_RXD"),
	PINCTRL_PIN(53, "GPIO53/SI2_nDTR_BOUT2/BU5_TXD"),
	PINCTRL_PIN(54, "GPIO54/SI2_nDSR/BU4_TXD"),
	PINCTRL_PIN(55, "GPIO55/SI2_RI2/BU4_RXD"),
	PINCTRL_PIN(56, "GPIO56/R1_RXERR/R1_OEN"),
	PINCTRL_PIN(57, "GPIO57/R1_MDC/TP_GPIO4"),
	PINCTRL_PIN(58, "GPIO58/R1_MDIO/TP_GPIO5"),
	PINCTRL_PIN(59, "GPIO59/H_GPIO06/SMB3D_SDA/SMB19_SDA"),
	PINCTRL_PIN(60, "GPIO60/H_GPIO07/SMB3D_SCL/SMB19_SCL"),
	PINCTRL_PIN(61, "GPIO61/SI1_nDTR_BOUT"),
	PINCTRL_PIN(62, "GPIO62/SI1_nRTS/BU1_nRTS/CP_TDO_SWO/TP_TDO_SWO/CP_TP_TDO_SWO"),
	PINCTRL_PIN(63, "GPIO63/BU1_TXD1/SI1_TXD"),
	PINCTRL_PIN(64, "GPIO64/FANIN0"),
	PINCTRL_PIN(65, "GPIO65/FANIN1"),
	PINCTRL_PIN(66, "GPIO66/FANIN2"),
	PINCTRL_PIN(67, "GPIO67/FANIN3"),
	PINCTRL_PIN(68, "GPIO68/FANIN4"),
	PINCTRL_PIN(69, "GPIO69/FANIN5"),
	PINCTRL_PIN(70, "GPIO70/FANIN6"),
	PINCTRL_PIN(71, "GPIO71/FANIN7"),
	PINCTRL_PIN(72, "GPIO72/FANIN8"),
	PINCTRL_PIN(73, "GPIO73/FANIN9"),
	PINCTRL_PIN(74, "GPIO74/FANIN10"),
	PINCTRL_PIN(75, "GPIO75/FANIN11"),
	PINCTRL_PIN(76, "GPIO76/FANIN12"),
	PINCTRL_PIN(77, "GPIO77/FANIN13"),
	PINCTRL_PIN(78, "GPIO78/FANIN14"),
	PINCTRL_PIN(79, "GPIO79/FANIN15"),
	PINCTRL_PIN(80, "GPIO80/PWM0"),
	PINCTRL_PIN(81, "GPIO81/PWM1"),
	PINCTRL_PIN(82, "GPIO82/PWM2"),
	PINCTRL_PIN(83, "GPIO83/PWM3"),
	PINCTRL_PIN(84, "GPIO84/R2_TXD0"),
	PINCTRL_PIN(85, "GPIO85/R2_TXD1"),
	PINCTRL_PIN(86, "GPIO86/R2_TXEN"),
	PINCTRL_PIN(87, "GPIO87/R2_RXD0"),
	PINCTRL_PIN(88, "GPIO88/R2_RXD1"),
	PINCTRL_PIN(89, "GPIO89/R2_CRSDV"),
	PINCTRL_PIN(90, "GPIO90/R2_RXERR/R2_OEN"),
	PINCTRL_PIN(91, "GPIO91/R2_MDC/CP1_GPIO6/TP_GPIO0"),
	PINCTRL_PIN(92, "GPIO92/R2_MDIO/CP1_GPIO7/TP_GPIO1"),
	PINCTRL_PIN(93, "GPIO93/GA20/SMB5D_SCL"),
	PINCTRL_PIN(94, "GPIO94/nKBRST/SMB5D_SDA"),
	PINCTRL_PIN(95, "GPIO95/nESPIRST/LPC_nLRESET"),
	PINCTRL_PIN(96, "GPIO96/CP1_GPIO7/BU2_TXD/TP_GPIO7"),
	PINCTRL_PIN(97, "GPIO97/CP1_GPIO6/BU2_RXD/TP_GPIO6"),
	PINCTRL_PIN(98, "GPIO98/CP1_GPIO5/BU4_TXD/TP_GPIO5"),
	PINCTRL_PIN(99, "GPIO99/CP1_GPIO4/BU4_RXD/TP_GPIO4"),
	PINCTRL_PIN(100, "GPIO100/CP1_GPIO3/BU5_TXD/TP_GPIO3"),
	PINCTRL_PIN(101, "GPIO101/CP1_GPIO2/BU5_RXD/TP_GPIO2"),
	PINCTRL_PIN(102, "GPIO102/HSYNC"),
	PINCTRL_PIN(103, "GPIO103/VSYNC"),
	PINCTRL_PIN(104, "GPIO104/DDC_SCL"),
	PINCTRL_PIN(105, "GPIO105/DDC_SDA"),
	PINCTRL_PIN(106, "GPIO106/I3C5_SCL"),
	PINCTRL_PIN(107, "GPIO107/I3C5_SDA"),
	PINCTRL_PIN(108, "GPIO108/SG1_MDC"),
	PINCTRL_PIN(109, "GPIO109/SG1_MDIO"),
	PINCTRL_PIN(110, "GPIO110/RG2_TXD0/DDRV0/R3_TXD0"),
	PINCTRL_PIN(111, "GPIO111/RG2_TXD1/DDRV1/R3_TXD1"),
	PINCTRL_PIN(112, "GPIO112/RG2_TXD2/DDRV2"),
	PINCTRL_PIN(113, "GPIO113/RG2_TXD3/DDRV3"),
	PINCTRL_PIN(114, "GPIO114/SMB0_SCL"),
	PINCTRL_PIN(115, "GPIO115/SMB0_SDA"),
	PINCTRL_PIN(116, "GPIO116/SMB1_SCL"),
	PINCTRL_PIN(117, "GPIO117/SMB1_SDA"),
	PINCTRL_PIN(118, "GPIO118/SMB2_SCL"),
	PINCTRL_PIN(119, "GPIO119/SMB2_SDA"),
	PINCTRL_PIN(120, "GPIO120/SMB2C_SDA"),
	PINCTRL_PIN(121, "GPIO121/SMB2C_SCL"),
	PINCTRL_PIN(122, "GPIO122/SMB2B_SDA"),
	PINCTRL_PIN(123, "GPIO123/SMB2B_SCL"),
	PINCTRL_PIN(124, "GPIO124/SMB1C_SDA/CP1_GPIO3"),
	PINCTRL_PIN(125, "GPIO125/SMB1C_SCL/CP1_GPIO2"),
	PINCTRL_PIN(126, "GPIO126/SMB1B_SDA/CP1_GPIO1"),
	PINCTRL_PIN(127, "GPIO127/SMB1B_SCL/CP1_GPIO0"),
	PINCTRL_PIN(128, "GPIO128/SMB824_SCL"),
	PINCTRL_PIN(129, "GPIO129/SMB824_SDA"),
	PINCTRL_PIN(130, "GPIO130/SMB925_SCL"),
	PINCTRL_PIN(131, "GPIO131/SMB925_SDA"),
	PINCTRL_PIN(132, "GPIO132/SMB1026_SCL"),
	PINCTRL_PIN(133, "GPIO133/SMB1026_SDA"),
	PINCTRL_PIN(134, "GPIO134/SMB11_SCL/SMB23B_SCL"),
	PINCTRL_PIN(135, "GPIO135/SMB11_SDA/SMB23B_SDA"),
	PINCTRL_PIN(136, "GPIO136/JM1_TCK"),
	PINCTRL_PIN(137, "GPIO137/JM1_TDO"),
	PINCTRL_PIN(138, "GPIO138/JM1_TMS"),
	PINCTRL_PIN(139, "GPIO139/JM1_TDI"),
	PINCTRL_PIN(140, "GPIO140/JM1_nTRST"),
	PINCTRL_PIN(141, "GPIO141/SMB7B_SCL"),
	PINCTRL_PIN(142, "GPIO142/SMB7D_SCL/TPSMB1_SCL"),
	PINCTRL_PIN(143, "GPIO143/SMB7D_SDA/TPSMB1_SDA"),
	PINCTRL_PIN(144, "GPIO144/PWM4"),
	PINCTRL_PIN(145, "GPIO145/PWM5"),
	PINCTRL_PIN(146, "GPIO146/PWM6"),
	PINCTRL_PIN(147, "GPIO147/PWM7"),
	PINCTRL_PIN(148, "GPIO148/MMC_DT4"),
	PINCTRL_PIN(149, "GPIO149/MMC_DT5"),
	PINCTRL_PIN(150, "GPIO150/MMC_DT6"),
	PINCTRL_PIN(151, "GPIO151/MMC_DT7"),
	PINCTRL_PIN(152, "GPIO152/MMC_CLK"),
	PINCTRL_PIN(153, "GPIO153/MMC_WP"),
	PINCTRL_PIN(154, "GPIO154/MMC_CMD"),
	PINCTRL_PIN(155, "GPIO155/MMC_nCD/MMC_nRSTLK"),
	PINCTRL_PIN(156, "GPIO156/MMC_DT0"),
	PINCTRL_PIN(157, "GPIO157/MMC_DT1"),
	PINCTRL_PIN(158, "GPIO158/MMC_DT2"),
	PINCTRL_PIN(159, "GPIO159/MMC_DT3"),
	PINCTRL_PIN(160, "GPIO160/CLKOUT/RNGOSCOUT/GFXBYPCK"),
	PINCTRL_PIN(161, "GPIO161/ESPI_nCS/LPC_nLFRAME"),
	PINCTRL_PIN(162, "GPIO162/SERIRQ"),
	PINCTRL_PIN(163, "GPIO163/ESPI_CK/LPC_LCLK"),
	PINCTRL_PIN(164, "GPIO164/ESPI_IO0/LPC_LAD0"),
	PINCTRL_PIN(165, "GPIO165/ESPI_IO1/LPC_LAD1"),
	PINCTRL_PIN(166, "GPIO166/ESPI_IO2/LPC_LAD2"),
	PINCTRL_PIN(167, "GPIO167/ESPI_IO3/LPC_LAD3"),
	PINCTRL_PIN(168, "GPIO168/ESPI_nALERT/LPC_nCLKRUN"),
	PINCTRL_PIN(169, "GPIO169/nSCIPME/SMB21_SCL"),
	PINCTRL_PIN(170, "GPIO170/nSMI/SMB21_SDA"),
	PINCTRL_PIN(171, "GPIO171/SMB6_SCL"),
	PINCTRL_PIN(172, "GPIO172/SMB6_SDA"),
	PINCTRL_PIN(173, "GPIO173/SMB7_SCL"),
	PINCTRL_PIN(174, "GPIO174/SMB7_SDA"),
	PINCTRL_PIN(175, "GPIO175/SPI1_CK/FANIN19/FM1_CK"),
	PINCTRL_PIN(176, "GPIO176/SPI1_DO/FANIN18/FM1_DO/STRAP9"),
	PINCTRL_PIN(177, "GPIO177/SPI1_DI/FANIN17/FM1_D1/STRAP10"),
	PINCTRL_PIN(178, "GPIO178/R1_TXD0"),
	PINCTRL_PIN(179, "GPIO179/R1_TXD1"),
	PINCTRL_PIN(180, "GPIO180/R1_TXEN"),
	PINCTRL_PIN(181, "GPIO181/R1_RXD0"),
	PINCTRL_PIN(182, "GPIO182/R1_RXD1"),
	PINCTRL_PIN(183, "GPIO183/SPI3_SEL"),
	PINCTRL_PIN(184, "GPIO184/SPI3_D0/STRAP13"),
	PINCTRL_PIN(185, "GPIO185/SPI3_D1"),
	PINCTRL_PIN(186, "GPIO186/SPI3_nCS0"),
	PINCTRL_PIN(187, "GPO187/SPI3_nCS1_SMB14B_SDA"),
	PINCTRL_PIN(188, "GPIO188/SPI3_D2/SPI3_nCS2"),
	PINCTRL_PIN(189, "GPIO189/SPI3_D3/SPI3_nCS3"),
	PINCTRL_PIN(190, "GPIO190/nPRD_SMI"),
	PINCTRL_PIN(191, "GPIO191/SPI1_D1/FANIN17/FM1_D1/STRAP10"),
	PINCTRL_PIN(192, "GPIO192/SPI1_D3/SPI_nCS3/FM1_D3/SMB15_SCL"),
	PINCTRL_PIN(193, "GPIO193/R1_CRSDV"),
	PINCTRL_PIN(194, "GPIO194/SMB0B_SCL/FM0_CK"),
	PINCTRL_PIN(195, "GPIO195/SMB0B_SDA/FM0_D0"),
	PINCTRL_PIN(196, "GPIO196/SMB0C_SCL/FM0_D1"),
	PINCTRL_PIN(197, "GPIO197/SMB0DEN/FM0_D3"),
	PINCTRL_PIN(198, "GPIO198/SMB0D_SDA/FM0_D2"),
	PINCTRL_PIN(199, "GPIO199/SMB0D_SCL/FM0_CSO"),
	PINCTRL_PIN(200, "GPIO200/R2_CK"),
	PINCTRL_PIN(201, "GPIO201/R1_CK"),
	PINCTRL_PIN(202, "GPIO202/SMB0C_SDA/FM0_CSI"),
	PINCTRL_PIN(203, "GPIO203/SPI1_nCS0/FANIN16/FM1_CSI"),
	PINCTRL_PIN(208, "GPIO208/RG2_TXC/DVCK"),
	PINCTRL_PIN(209, "GPIO209/RG2_TXCTL/DDRV4/R3_TXEN"),
	PINCTRL_PIN(210, "GPIO210/RG2_RXD0/DDRV5/R3_RXD0"),
	PINCTRL_PIN(211, "GPIO211/RG2_RXD1/DDRV6/R3_RXD1"),
	PINCTRL_PIN(212, "GPIO212/RG2_RXD2/DDRV7/R3_RXD2"),
	PINCTRL_PIN(213, "GPIO213/RG2_RXD3/DDRV8/R3_OEN"),
	PINCTRL_PIN(214, "GPIO214/RG2_RXC/DDRV9/R3_CK"),
	PINCTRL_PIN(215, "GPIO215/RG2_RXCTL/DDRV10/R3_CRSDV"),
	PINCTRL_PIN(216, "GPIO216/RG2_MDC/DDRV11"),
	PINCTRL_PIN(217, "GPIO217/RG2_MDIO/DVHSYNC"),
	PINCTRL_PIN(218, "GPIO218/nWDO1/SMB16_SCL"),
	PINCTRL_PIN(219, "GPIO219/nWDO2/SMB16_SDA"),
	PINCTRL_PIN(220, "GPIO220/SMB12_SCL/PWM8"),
	PINCTRL_PIN(221, "GPIO221/SMB12_SDA/PWM9"),
	PINCTRL_PIN(222, "GPIO222/SMB13_SCL"),
	PINCTRL_PIN(223, "GPIO223/SMB13_SDA"),
	PINCTRL_PIN(224, "GPIO224/SPIX_CK/FM2_CK"),
	PINCTRL_PIN(225, "GPO225/SPIX_D0/FM2_D0/STRAP1"),
	PINCTRL_PIN(226, "GPO226/SPIX_D1/FM2_D1/STRAP2"),
	PINCTRL_PIN(227, "GPIO227/SPIX_nCS0/FM2_CSI"),
	PINCTRL_PIN(228, "GPIO228/SPIX_nCS1/FM2_CSO"),
	PINCTRL_PIN(229, "GPO229/SPIX_D2/FM2_D2/STRAP3"),
	PINCTRL_PIN(230, "GPO230/SPIX_D3/FM2_D3/STRAP6"),
	PINCTRL_PIN(231, "GPIO231/EP_nCLKREQ"),
	PINCTRL_PIN(233, "GPIO233/SPI1_nCS1/FM1_CSO"),
	PINCTRL_PIN(234, "GPIO234/PWM10/SMB20_SCL"),
	PINCTRL_PIN(235, "GPIO235/PWM11/SMB20_SDA"),
	PINCTRL_PIN(240, "GPIO240/I3C0_SCL"),
	PINCTRL_PIN(241, "GPIO241/I3C0_SDA"),
	PINCTRL_PIN(242, "GPIO242/I3C1_SCL"),
	PINCTRL_PIN(243, "GPIO243/I3C1_SDA"),
	PINCTRL_PIN(244, "GPIO244/I3C2_SCL"),
	PINCTRL_PIN(245, "GPIO245/I3C2_SDA"),
	PINCTRL_PIN(246, "GPIO246/I3C3_SCL"),
	PINCTRL_PIN(247, "GPIO247/I3C3_SDA"),
	PINCTRL_PIN(250, "GPIO250/RG2_REFCK/DVVSYNC"),
	PINCTRL_PIN(251, "JM2/CP1_GPIO"),
	};

/* Enable mode in pin group */
static void npcm8xx_setfunc(struct regmap *gcr_regmap, const unsigned int *pin,
			    int pin_number, int mode)
{
	const struct npcm8xx_pincfg *cfg;
	int i;

	for (i = 0 ; i < pin_number ; i++) {
		cfg = &pincfg[pin[i]];
		if (mode == fn_gpio || cfg->fn0 == mode || cfg->fn1 == mode ||
		    cfg->fn2 == mode || cfg->fn3 == mode || cfg->fn4 == mode) {
			if (cfg->reg0)
				regmap_update_bits(gcr_regmap, cfg->reg0,
						   BIT(cfg->bit0),
						   (cfg->fn0 == mode) ?
						   BIT(cfg->bit0) : 0);
			if (cfg->reg1)
				regmap_update_bits(gcr_regmap, cfg->reg1,
						   BIT(cfg->bit1),
						   (cfg->fn1 == mode) ?
						   BIT(cfg->bit1) : 0);
			if (cfg->reg2)
				regmap_update_bits(gcr_regmap, cfg->reg2,
						   BIT(cfg->bit2),
						   (cfg->fn2 == mode) ?
						   BIT(cfg->bit2) : 0);
			if (cfg->reg3)
				regmap_update_bits(gcr_regmap, cfg->reg3,
						   BIT(cfg->bit3),
						   (cfg->fn3 == mode) ?
						   BIT(cfg->bit3) : 0);
			if (cfg->reg4)
				regmap_update_bits(gcr_regmap, cfg->reg4,
						   BIT(cfg->bit4),
						   (cfg->fn4 == mode) ?
						   BIT(cfg->bit4) : 0);
		}
	}
}

static int npcm8xx_get_slew_rate(struct npcm8xx_gpio *bank,
				 struct regmap *gcr_regmap, unsigned int pin)
{
	int gpio = pin % bank->gc.ngpio;
	unsigned long pinmask = BIT(gpio);
	u32 val;

	if (pincfg[pin].flag & SLEW)
		return ioread32(bank->base + NPCM8XX_GP_N_OSRC) & pinmask;
	/* LPC Slew rate in SRCNT register */
	if (pincfg[pin].flag & SLEWLPC) {
		regmap_read(gcr_regmap, NPCM8XX_GCR_SRCNT, &val);
		return !!(val & SRCNT_ESPI);
	}

	return -EINVAL;
}

static int npcm8xx_set_slew_rate(struct npcm8xx_gpio *bank,
				 struct regmap *gcr_regmap, unsigned int pin,
				 int arg)
{
	void __iomem *OSRC_Offset = bank->base + NPCM8XX_GP_N_OSRC;
	int gpio = BIT(pin % bank->gc.ngpio);

	if (pincfg[pin].flag & SLEW) {
		switch (arg) {
		case 0:
			npcm_gpio_clr(&bank->gc, OSRC_Offset, gpio);
			return 0;
		case 1:
			npcm_gpio_set(&bank->gc, OSRC_Offset, gpio);
			return 0;
		default:
			return -EINVAL;
		}
	}

	if (!(pincfg[pin].flag & SLEWLPC))
		return -EINVAL;

	switch (arg) {
	case 0:
		regmap_update_bits(gcr_regmap, NPCM8XX_GCR_SRCNT,
				   SRCNT_ESPI, 0);
		break;
	case 1:
		regmap_update_bits(gcr_regmap, NPCM8XX_GCR_SRCNT,
				   SRCNT_ESPI, SRCNT_ESPI);
		break;
	default:
		return -EINVAL;
		}

	return 0;
}

static int npcm8xx_get_drive_strength(struct pinctrl_dev *pctldev,
				      unsigned int pin)
{
	struct npcm8xx_pinctrl *npcm = pinctrl_dev_get_drvdata(pctldev);
	struct npcm8xx_gpio *bank =
		&npcm->gpio_bank[pin / NPCM8XX_GPIO_PER_BANK];
	int gpio = pin % bank->gc.ngpio;
	unsigned long pinmask = BIT(gpio);
	int flg, val;
	u32 ds = 0;

	flg = pincfg[pin].flag;
	if (!(flg & DRIVE_STRENGTH_MASK))
		return -EINVAL;

	val = ioread32(bank->base + NPCM8XX_GP_N_ODSC) & pinmask;
	ds = val ? DSHI(flg) : DSLO(flg);
	dev_dbg(bank->gc.parent, "pin %d strength %d = %d\n", pin, val, ds);

	return ds;
}

static int npcm8xx_set_drive_strength(struct npcm8xx_pinctrl *npcm,
				      unsigned int pin, int nval)
{
	struct npcm8xx_gpio *bank =
		&npcm->gpio_bank[pin / NPCM8XX_GPIO_PER_BANK];
	int gpio = BIT(pin % bank->gc.ngpio);
	int v;

	v = pincfg[pin].flag & DRIVE_STRENGTH_MASK;

	if (DSLO(v) == nval)
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_ODSC, gpio);
	else if (DSHI(v) == nval)
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_ODSC, gpio);
	else
		return -ENOTSUPP;

	return 0;
}

/* pinctrl_ops */
static int npcm8xx_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(npcm8xx_pingroups);
}

static const char *npcm8xx_get_group_name(struct pinctrl_dev *pctldev,
					  unsigned int selector)
{
	return npcm8xx_pingroups[selector].name;
}

static int npcm8xx_get_group_pins(struct pinctrl_dev *pctldev,
				  unsigned int selector,
				  const unsigned int **pins,
				  unsigned int *npins)
{
	*npins = npcm8xx_pingroups[selector].npins;
	*pins  = npcm8xx_pingroups[selector].pins;

	return 0;
}

static int npcm8xx_dt_node_to_map(struct pinctrl_dev *pctldev,
				  struct device_node *np_config,
				  struct pinctrl_map **map,
				  u32 *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np_config,
					      map, num_maps,
					      PIN_MAP_TYPE_INVALID);
}

static void npcm8xx_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, u32 num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops npcm8xx_pinctrl_ops = {
	.get_groups_count = npcm8xx_get_groups_count,
	.get_group_name = npcm8xx_get_group_name,
	.get_group_pins = npcm8xx_get_group_pins,
	.dt_node_to_map = npcm8xx_dt_node_to_map,
	.dt_free_map = npcm8xx_dt_free_map,
};

static int npcm8xx_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(npcm8xx_funcs);
}

static const char *npcm8xx_get_function_name(struct pinctrl_dev *pctldev,
					     unsigned int function)
{
	return npcm8xx_funcs[function].name;
}

static int npcm8xx_get_function_groups(struct pinctrl_dev *pctldev,
				       unsigned int function,
				       const char * const **groups,
				       unsigned int * const ngroups)
{
	*ngroups = npcm8xx_funcs[function].ngroups;
	*groups	 = npcm8xx_funcs[function].groups;

	return 0;
}

static int npcm8xx_pinmux_set_mux(struct pinctrl_dev *pctldev,
				  unsigned int function,
				  unsigned int group)
{
	struct npcm8xx_pinctrl *npcm = pinctrl_dev_get_drvdata(pctldev);

	npcm8xx_setfunc(npcm->gcr_regmap, npcm8xx_pingroups[group].pins,
			npcm8xx_pingroups[group].npins, group);

	return 0;
}

static int npcm8xx_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned int offset)
{
	struct npcm8xx_pinctrl *npcm = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pin = &offset;
	int mode = fn_gpio;

	if ((pin[0] >= 183 && pin[0] <= 189) || pin[0] == 35 || pin[0] == 36)
		mode = pincfg[pin[0]].fn0;

	npcm8xx_setfunc(npcm->gcr_regmap, &offset, 1, mode);

	return 0;
}

static void npcm8xx_gpio_request_free(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	struct npcm8xx_pinctrl *npcm = pinctrl_dev_get_drvdata(pctldev);
	int virq;

	virq = irq_find_mapping(npcm->domain, offset);
	if (virq)
		irq_dispose_mapping(virq);
}

static int npcm_gpio_set_direction(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned int offset, bool input)
{
	struct npcm8xx_pinctrl *npcm = pinctrl_dev_get_drvdata(pctldev);
	struct npcm8xx_gpio *bank =
		&npcm->gpio_bank[offset / NPCM8XX_GPIO_PER_BANK];
	int gpio = BIT(offset % bank->gc.ngpio);

	if (input)
		iowrite32(gpio, bank->base + NPCM8XX_GP_N_OEC);
	else
		iowrite32(gpio, bank->base + NPCM8XX_GP_N_OES);

	return 0;
}

static const struct pinmux_ops npcm8xx_pinmux_ops = {
	.get_functions_count = npcm8xx_get_functions_count,
	.get_function_name = npcm8xx_get_function_name,
	.get_function_groups = npcm8xx_get_function_groups,
	.set_mux = npcm8xx_pinmux_set_mux,
	.gpio_request_enable = npcm8xx_gpio_request_enable,
	.gpio_disable_free = npcm8xx_gpio_request_free,
	.gpio_set_direction = npcm_gpio_set_direction,
};

static int debounce_timing_setting(struct npcm8xx_gpio *bank, u32 gpio,
				   u32 nanosecs)
{
	void __iomem *DBNCS_offset = bank->base + NPCM8XX_GP_N_DBNCS0 + (gpio / 4);
	int gpio_debounce = (gpio % 16) * 2, debounce_select, i;
	u32 dbncp_val, dbncp_val_mod;

	for (i = 0 ; i < NPCM8XX_DEBOUNCE_MAX ; i++) {
		if (bank->debounce.set_val[i]) {
			if (bank->debounce.nanosec_val[i] == nanosecs) {
				debounce_select = i << gpio_debounce;
				npcm_gpio_set(&bank->gc, DBNCS_offset,
					      debounce_select);
				break;
			}
		} else {
			bank->debounce.set_val[i] = true;
			bank->debounce.nanosec_val[i] = nanosecs;
			debounce_select = i << gpio_debounce;
			npcm_gpio_set(&bank->gc, DBNCS_offset, debounce_select);
			switch (nanosecs) {
			case 1 ... 1040:
				iowrite32(0, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			case 1041 ... 1640:
				iowrite32(0x10, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			case 1641 ... 2280:
				iowrite32(0x20, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			case 2281 ... 2700:
				iowrite32(0x30, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			case 2701 ... 2856:
				iowrite32(0x40, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			case 2857 ... 3496:
				iowrite32(0x50, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			case 3497 ... 4136:
				iowrite32(0x60, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			case 4137 ... 5025:
				iowrite32(0x70, bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			default:
				dbncp_val = DIV_ROUND_CLOSEST(nanosecs, NPCM8XX_DEBOUNCE_NSEC);
				if (dbncp_val > NPCM8XX_DEBOUNCE_MAX_VAL)
					return -ENOTSUPP;
				dbncp_val_mod = dbncp_val & GENMASK(3, 0);
				if (dbncp_val_mod > GENMASK(2, 0))
					dbncp_val += 0x10;
				iowrite32(dbncp_val & NPCM8XX_DEBOUNCE_VAL_MASK,
					  bank->base + NPCM8XX_GP_N_DBNCP0 + (i * 4));
				break;
			}
			break;
		}
	}

	if (i == 4)
		return -ENOTSUPP;

	return 0;
}

static int npcm_set_debounce(struct npcm8xx_pinctrl *npcm, unsigned int pin,
			     u32 nanosecs)
{
	struct npcm8xx_gpio *bank =
		&npcm->gpio_bank[pin / NPCM8XX_GPIO_PER_BANK];
	int gpio = BIT(pin % bank->gc.ngpio);
	int ret;

	if (nanosecs) {
		ret = debounce_timing_setting(bank, pin % bank->gc.ngpio,
					      nanosecs);
		if (ret)
			dev_err(npcm->dev, "Pin %d, All four debounce timing values are used, please use one of exist debounce values\n", pin);
		else
			npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_DBNC,
				      gpio);
		return ret;
	}

	npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_DBNC, gpio);

	return 0;
}

/* pinconf_ops */
static int npcm8xx_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	enum pin_config_param param = pinconf_to_config_param(*config);
	struct npcm8xx_pinctrl *npcm = pinctrl_dev_get_drvdata(pctldev);
	struct npcm8xx_gpio *bank =
		&npcm->gpio_bank[pin / NPCM8XX_GPIO_PER_BANK];
	int gpio = pin % bank->gc.ngpio;
	unsigned long pinmask = BIT(gpio);
	u32 ie, oe, pu, pd;
	int rc = 0;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		pu = ioread32(bank->base + NPCM8XX_GP_N_PU) & pinmask;
		pd = ioread32(bank->base + NPCM8XX_GP_N_PD) & pinmask;
		if (param == PIN_CONFIG_BIAS_DISABLE)
			rc = !pu && !pd;
		else if (param == PIN_CONFIG_BIAS_PULL_UP)
			rc = pu && !pd;
		else if (param == PIN_CONFIG_BIAS_PULL_DOWN)
			rc = !pu && pd;
		break;
	case PIN_CONFIG_OUTPUT:
	case PIN_CONFIG_INPUT_ENABLE:
		ie = ioread32(bank->base + NPCM8XX_GP_N_IEM) & pinmask;
		oe = ioread32(bank->base + NPCM8XX_GP_N_OE) & pinmask;
		if (param == PIN_CONFIG_INPUT_ENABLE)
			rc = (ie && !oe);
		else if (param == PIN_CONFIG_OUTPUT)
			rc = (!ie && oe);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		rc = !(ioread32(bank->base + NPCM8XX_GP_N_OTYP) & pinmask);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		rc = ioread32(bank->base + NPCM8XX_GP_N_OTYP) & pinmask;
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		rc = ioread32(bank->base + NPCM8XX_GP_N_DBNC) & pinmask;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		rc = npcm8xx_get_drive_strength(pctldev, pin);
		if (rc)
			*config = pinconf_to_config_packed(param, rc);
		break;
	case PIN_CONFIG_SLEW_RATE:
		rc = npcm8xx_get_slew_rate(bank, npcm->gcr_regmap, pin);
		if (rc >= 0)
			*config = pinconf_to_config_packed(param, rc);
		break;
	default:
		return -ENOTSUPP;
	}

	if (!rc)
		return -EINVAL;

	return 0;
}

static int npcm8xx_config_set_one(struct npcm8xx_pinctrl *npcm,
				  unsigned int pin, unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);
	struct npcm8xx_gpio *bank =
		&npcm->gpio_bank[pin / NPCM8XX_GPIO_PER_BANK];
	u32 arg = pinconf_to_config_argument(config);
	int gpio = BIT(pin % bank->gc.ngpio);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_PU, gpio);
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_PD, gpio);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_PU, gpio);
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_PD, gpio);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_PD, gpio);
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_PU, gpio);
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		iowrite32(gpio, bank->base + NPCM8XX_GP_N_OEC);
		bank->direction_input(&bank->gc, pin % bank->gc.ngpio);
		break;
	case PIN_CONFIG_OUTPUT:
		bank->direction_output(&bank->gc, pin % bank->gc.ngpio, arg);
		iowrite32(gpio, bank->base + NPCM8XX_GP_N_OES);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		npcm_gpio_clr(&bank->gc, bank->base + NPCM8XX_GP_N_OTYP, gpio);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		npcm_gpio_set(&bank->gc, bank->base + NPCM8XX_GP_N_OTYP, gpio);
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		return npcm_set_debounce(npcm, pin, arg * 1000);
	case PIN_CONFIG_SLEW_RATE:
		return npcm8xx_set_slew_rate(bank, npcm->gcr_regmap, pin, arg);
	case PIN_CONFIG_DRIVE_STRENGTH:
		return npcm8xx_set_drive_strength(npcm, pin, arg);
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int npcm8xx_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct npcm8xx_pinctrl *npcm = pinctrl_dev_get_drvdata(pctldev);
	int rc;

	while (num_configs--) {
		rc = npcm8xx_config_set_one(npcm, pin, *configs++);
		if (rc)
			return rc;
	}

	return 0;
}

static const struct pinconf_ops npcm8xx_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = npcm8xx_config_get,
	.pin_config_set = npcm8xx_config_set,
};

/* pinctrl_desc */
static struct pinctrl_desc npcm8xx_pinctrl_desc = {
	.name = "npcm8xx-pinctrl",
	.pins = npcm8xx_pins,
	.npins = ARRAY_SIZE(npcm8xx_pins),
	.pctlops = &npcm8xx_pinctrl_ops,
	.pmxops = &npcm8xx_pinmux_ops,
	.confops = &npcm8xx_pinconf_ops,
	.owner = THIS_MODULE,
};

static int npcmgpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct npcm8xx_gpio *bank = gpiochip_get_data(chip);

	return gpiochip_add_pin_range(&bank->gc, dev_name(chip->parent),
				      bank->pinctrl_id, bank->gc.base,
				      bank->gc.ngpio);
}

static int npcm8xx_gpio_fw(struct npcm8xx_pinctrl *pctrl)
{
	struct fwnode_reference_args args;
	struct device *dev = pctrl->dev;
	struct fwnode_handle *child;
	int ret = -ENXIO;
	int id = 0, i;

	for_each_gpiochip_node(dev, child) {
		pctrl->gpio_bank[id].base = fwnode_iomap(child, 0);
		if (!pctrl->gpio_bank[id].base)
			return dev_err_probe(dev, -ENXIO, "fwnode_iomap id %d failed\n", id);

		ret = bgpio_init(&pctrl->gpio_bank[id].gc, dev, 4,
				 pctrl->gpio_bank[id].base + NPCM8XX_GP_N_DIN,
				 pctrl->gpio_bank[id].base + NPCM8XX_GP_N_DOUT,
				 NULL,
				 NULL,
				 pctrl->gpio_bank[id].base + NPCM8XX_GP_N_IEM,
				 BGPIOF_READ_OUTPUT_REG_SET);
		if (ret)
			return dev_err_probe(dev, ret, "bgpio_init() failed\n");

		ret = fwnode_property_get_reference_args(child, "gpio-ranges", NULL, 3, 0, &args);
		if (ret < 0)
			return dev_err_probe(dev, ret, "gpio-ranges fail for GPIO bank %u\n", id);

		ret = fwnode_irq_get(child, 0);
		if (!ret)
			return dev_err_probe(dev, ret, "No IRQ for GPIO bank %u\n", id);

		pctrl->gpio_bank[id].irq = ret;
		pctrl->gpio_bank[id].irq_chip = npcmgpio_irqchip;
		pctrl->gpio_bank[id].irqbase = id * NPCM8XX_GPIO_PER_BANK;
		pctrl->gpio_bank[id].pinctrl_id = args.args[0];
		pctrl->gpio_bank[id].gc.base = -1;
		pctrl->gpio_bank[id].gc.ngpio = args.args[2];
		pctrl->gpio_bank[id].gc.owner = THIS_MODULE;
		pctrl->gpio_bank[id].gc.parent = dev;
		pctrl->gpio_bank[id].gc.fwnode = child;
		pctrl->gpio_bank[id].gc.label = devm_kasprintf(dev, GFP_KERNEL, "%pfw", child);
		pctrl->gpio_bank[id].gc.dbg_show = npcmgpio_dbg_show;
		pctrl->gpio_bank[id].direction_input = pctrl->gpio_bank[id].gc.direction_input;
		pctrl->gpio_bank[id].gc.direction_input = npcmgpio_direction_input;
		pctrl->gpio_bank[id].direction_output = pctrl->gpio_bank[id].gc.direction_output;
		pctrl->gpio_bank[id].gc.direction_output = npcmgpio_direction_output;
		pctrl->gpio_bank[id].request = pctrl->gpio_bank[id].gc.request;
		pctrl->gpio_bank[id].gc.request = npcmgpio_gpio_request;
		pctrl->gpio_bank[id].gc.free = pinctrl_gpio_free;
		for (i = 0 ; i < NPCM8XX_DEBOUNCE_MAX ; i++)
			pctrl->gpio_bank[id].debounce.set_val[i] = false;
		pctrl->gpio_bank[id].gc.add_pin_ranges = npcmgpio_add_pin_ranges;
		id++;
	}

	pctrl->bank_num = id;
	return ret;
}

static int npcm8xx_gpio_register(struct npcm8xx_pinctrl *pctrl)
{
	int ret, id;

	for (id = 0 ; id < pctrl->bank_num ; id++) {
		struct gpio_irq_chip *girq;

		girq = &pctrl->gpio_bank[id].gc.irq;
		girq->chip = &pctrl->gpio_bank[id].irq_chip;
		girq->parent_handler = npcmgpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(pctrl->dev, girq->num_parents,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;

		girq->parents[0] = pctrl->gpio_bank[id].irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_level_irq;
		ret = devm_gpiochip_add_data(pctrl->dev,
					     &pctrl->gpio_bank[id].gc,
					     &pctrl->gpio_bank[id]);
		if (ret)
			return dev_err_probe(pctrl->dev, ret, "Failed to add GPIO chip %u\n", id);
	}

	return 0;
}

static int npcm8xx_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct npcm8xx_pinctrl *pctrl;
	int ret;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = dev;
	platform_set_drvdata(pdev, pctrl);

	pctrl->gcr_regmap =
		syscon_regmap_lookup_by_phandle(dev->of_node, "nuvoton,sysgcr");
	if (IS_ERR(pctrl->gcr_regmap))
		return dev_err_probe(dev, PTR_ERR(pctrl->gcr_regmap),
				      "Failed to find nuvoton,sysgcr property\n");

	ret = npcm8xx_gpio_fw(pctrl);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				      "Failed to gpio dt-binding\n");

	pctrl->pctldev = devm_pinctrl_register(dev, &npcm8xx_pinctrl_desc, pctrl);
	if (IS_ERR(pctrl->pctldev))
		return dev_err_probe(dev, PTR_ERR(pctrl->pctldev),
				     "Failed to register pinctrl device\n");

	ret = npcm8xx_gpio_register(pctrl);
	if (ret < 0)
		dev_err_probe(dev, ret, "Failed to register gpio\n");

	return 0;
}

static const struct of_device_id npcm8xx_pinctrl_match[] = {
	{ .compatible = "nuvoton,npcm845-pinctrl" },
	{ }
};
MODULE_DEVICE_TABLE(of, npcm8xx_pinctrl_match);

static struct platform_driver npcm8xx_pinctrl_driver = {
	.probe = npcm8xx_pinctrl_probe,
	.driver = {
		.name = "npcm8xx-pinctrl",
		.of_match_table = npcm8xx_pinctrl_match,
		.suppress_bind_attrs = true,
	},
};

static int __init npcm8xx_pinctrl_register(void)
{
	return platform_driver_register(&npcm8xx_pinctrl_driver);
}
arch_initcall(npcm8xx_pinctrl_register);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("tomer.maimon@nuvoton.com");
MODULE_DESCRIPTION("Nuvoton NPCM8XX Pinctrl and GPIO driver");
