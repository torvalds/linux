// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/input/keyboard/pxa27x_keypad.c
 *
 * Driver for the pxa27x matrix keyboard controller.
 *
 * Created:	Feb 22, 2007
 * Author:	Rodolfo Giometti <giometti@linux.it>
 *
 * Based on a previous implementations by Kevin O'Connor
 * <kevin_at_koconnor.net> and Alex Osborne <bobofdoom@gmail.com> and
 * on some suggestions by Nicolas Pitre <nico@fluxnic.net>.
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/input/matrix_keypad.h>
#include <linux/slab.h>
#include <linux/of.h>

/*
 * Keypad Controller registers
 */
#define KPC		0x0000 /* Keypad Control register */
#define KPDK		0x0008 /* Keypad Direct Key register */
#define KPREC		0x0010 /* Keypad Rotary Encoder register */
#define KPMK		0x0018 /* Keypad Matrix Key register */
#define KPAS		0x0020 /* Keypad Automatic Scan register */

/* Keypad Automatic Scan Multiple Key Presser register 0-3 */
#define KPASMKP0	0x0028
#define KPASMKP1	0x0030
#define KPASMKP2	0x0038
#define KPASMKP3	0x0040
#define KPKDI		0x0048

/* bit definitions */
#define KPC_MKRN_MASK	GENMASK(28, 26)
#define KPC_MKCN_MASK	GENMASK(25, 23)
#define KPC_DKN_MASK	GENMASK(8, 6)
#define KPC_MKRN(n)	FIELD_PREP(KPC_MKRN_MASK, (n) - 1)
#define KPC_MKCN(n)	FIELD_PREP(KPC_MKCN_MASK, (n) - 1)
#define KPC_DKN(n)	FIELD_PREP(KPC_DKN_MASK, (n) - 1)

#define KPC_AS		BIT(30)  /* Automatic Scan bit */
#define KPC_ASACT	BIT(29)  /* Automatic Scan on Activity */
#define KPC_MI		BIT(22)  /* Matrix interrupt bit */
#define KPC_IMKP	BIT(21)  /* Ignore Multiple Key Press */

#define KPC_MS(n)	BIT(13 + (n))	/* Matrix scan line 'n' */
#define KPC_MS_ALL	GENMASK(20, 13)

#define KPC_ME		BIT(12)  /* Matrix Keypad Enable */
#define KPC_MIE		BIT(11)  /* Matrix Interrupt Enable */
#define KPC_DK_DEB_SEL	BIT(9)   /* Direct Keypad Debounce Select */
#define KPC_DI		BIT(5)   /* Direct key interrupt bit */
#define KPC_RE_ZERO_DEB	BIT(4)   /* Rotary Encoder Zero Debounce */
#define KPC_REE1	BIT(3)   /* Rotary Encoder1 Enable */
#define KPC_REE0	BIT(2)   /* Rotary Encoder0 Enable */
#define KPC_DE		BIT(1)   /* Direct Keypad Enable */
#define KPC_DIE		BIT(0)   /* Direct Keypad interrupt Enable */

#define KPDK_DKP	BIT(31)
#define KPDK_DK_MASK	GENMASK(7, 0)
#define KPDK_DK(n)	FIELD_GET(KPDK_DK_MASK, n)

#define KPREC_OF1	BIT(31)
#define KPREC_UF1	BIT(30)
#define KPREC_OF0	BIT(15)
#define KPREC_UF0	BIT(14)

#define KPREC_RECOUNT0_MASK	GENMASK(7, 0)
#define KPREC_RECOUNT1_MASK	GENMASK(23, 16)
#define KPREC_RECOUNT0(n)	FIELD_GET(KPREC_RECOUNT0_MASK, n)
#define KPREC_RECOUNT1(n)	FIELD_GET(KPREC_RECOUNT1_MASK, n)

#define KPMK_MKP	BIT(31)
#define KPAS_SO		BIT(31)
#define KPASMKPx_SO	BIT(31)

#define KPAS_MUKP_MASK	GENMASK(30, 26)
#define KPAS_RP_MASK	GENMASK(7, 4)
#define KPAS_CP_MASK	GENMASK(3, 0)
#define KPAS_MUKP(n)	FIELD_GET(KPAS_MUKP_MASK, n)
#define KPAS_RP(n)	FIELD_GET(KPAS_RP_MASK, n)
#define KPAS_CP(n)	FIELD_GET(KPAS_CP_MASK, n)

#define KPASMKP_MKC_MASK	GENMASK(7, 0)

#define keypad_readl(off)	__raw_readl(keypad->mmio_base + (off))
#define keypad_writel(off, v)	__raw_writel((v), keypad->mmio_base + (off))

#define MAX_MATRIX_KEY_ROWS	8
#define MAX_MATRIX_KEY_COLS	8
#define MAX_DIRECT_KEY_NUM	8
#define MAX_ROTARY_ENCODERS	2

#define MAX_MATRIX_KEY_NUM	(MAX_MATRIX_KEY_ROWS * MAX_MATRIX_KEY_COLS)
#define MAX_KEYPAD_KEYS		(MAX_MATRIX_KEY_NUM + MAX_DIRECT_KEY_NUM)

struct pxa27x_keypad_rotary {
	unsigned short *key_codes;
	int rel_code;
	bool enabled;
};

struct pxa27x_keypad {
	struct clk *clk;
	struct input_dev *input_dev;
	void __iomem *mmio_base;

	int irq;

	unsigned int matrix_key_rows;
	unsigned int matrix_key_cols;
	unsigned int row_shift;

	unsigned int direct_key_num;
	unsigned int direct_key_mask;
	bool direct_key_low_active;

	/* key debounce interval */
	unsigned int debounce_interval;

	unsigned short keycodes[MAX_KEYPAD_KEYS];

	/* state row bits of each column scan */
	u32 matrix_key_state[MAX_MATRIX_KEY_COLS];
	u32 direct_key_state;

	struct pxa27x_keypad_rotary rotary[MAX_ROTARY_ENCODERS];
};

static int pxa27x_keypad_matrix_key_parse(struct pxa27x_keypad *keypad)
{
	struct input_dev *input_dev = keypad->input_dev;
	struct device *dev = input_dev->dev.parent;
	int error;

	error = matrix_keypad_parse_properties(dev, &keypad->matrix_key_rows,
					       &keypad->matrix_key_cols);
	if (error)
		return error;

	if (keypad->matrix_key_rows > MAX_MATRIX_KEY_ROWS ||
	    keypad->matrix_key_cols > MAX_MATRIX_KEY_COLS) {
		dev_err(dev, "rows or cols exceeds maximum value\n");
		return -EINVAL;
	}

	keypad->row_shift = get_count_order(keypad->matrix_key_cols);

	error = matrix_keypad_build_keymap(NULL, NULL,
					   keypad->matrix_key_rows,
					   keypad->matrix_key_cols,
					   keypad->keycodes, input_dev);
	if (error)
		return error;

	return 0;
}

static int pxa27x_keypad_direct_key_parse(struct pxa27x_keypad *keypad)
{
	struct input_dev *input_dev = keypad->input_dev;
	struct device *dev = input_dev->dev.parent;
	unsigned short code;
	int count;
	int i;
	int error;

	error = device_property_read_u32(dev, "marvell,direct-key-count",
					 &keypad->direct_key_num);
	if (error) {
		/*
		 * If do not have marvel,direct-key-count defined,
		 * it means direct key is not supported.
		 */
		return error == -EINVAL ? 0 : error;
	}

	error = device_property_read_u32(dev, "marvell,direct-key-mask",
					 &keypad->direct_key_mask);
	if (error) {
		if (error != -EINVAL)
			return error;

		/*
		 * If marvell,direct-key-mask is not defined, driver will use
		 * a default value based on number of direct keys set up.
		 * The default value is calculated in pxa27x_keypad_config().
		 */
		keypad->direct_key_mask = 0;
	}

	keypad->direct_key_low_active =
		device_property_read_bool(dev, "marvell,direct-key-low-active");

	count = device_property_count_u16(dev, "marvell,direct-key-map");
	if (count <= 0 || count > MAX_DIRECT_KEY_NUM)
		return -EINVAL;

	error = device_property_read_u16_array(dev, "marvell,direct-key-map",
					       &keypad->keycodes[MAX_MATRIX_KEY_NUM],
					       count);

	for (i = 0; i < count; i++) {
		code = keypad->keycodes[MAX_MATRIX_KEY_NUM + i];
		__set_bit(code, input_dev->keybit);
	}

	return 0;
}

static int pxa27x_keypad_rotary_parse(struct pxa27x_keypad *keypad)
{
	static const char * const rotaryname[] = { "marvell,rotary0", "marvell,rotary1" };
	struct input_dev *input_dev = keypad->input_dev;
	struct device *dev = input_dev->dev.parent;
	struct pxa27x_keypad_rotary *encoder;
	unsigned int code;
	int i;
	int error;

	error = device_property_read_u32(dev, "marvell,rotary-rel-key", &code);
	if (!error) {
		for (i = 0; i < MAX_ROTARY_ENCODERS; i++, code >>= 16) {
			encoder = &keypad->rotary[i];
			encoder->enabled = true;
			encoder->rel_code = code & 0xffff;
			input_set_capability(input_dev, EV_REL, encoder->rel_code);
		}

		return 0;
	}

	for (i = 0; i < MAX_ROTARY_ENCODERS; i++) {
		encoder = &keypad->rotary[i];

		/*
		 * If the prop is not set, it means keypad does not need
		 * initialize the rotaryX.
		 */
		if (!device_property_present(dev, rotaryname[i]))
			continue;

		error = device_property_read_u32(dev, rotaryname[i], &code);
		if (error)
			return error;

		/*
		 * Not all up/down key code are valid.
		 * Now we depends on direct-rel-code.
		 */
		if (!(code & 0xffff) || !(code >> 16))
			return -EINVAL;

		encoder->enabled = true;
		encoder->rel_code = -1;
		encoder->key_codes = &keypad->keycodes[MAX_MATRIX_KEY_NUM + i * 2];
		encoder->key_codes[0] = code & 0xffff;
		encoder->key_codes[1] = code >> 16;

		input_set_capability(input_dev, EV_KEY, encoder->key_codes[0]);
		input_set_capability(input_dev, EV_KEY, encoder->key_codes[1]);
	}

	return 0;
}

static int pxa27x_keypad_parse_properties(struct pxa27x_keypad *keypad)
{
	struct input_dev *input_dev = keypad->input_dev;
	struct device *dev = input_dev->dev.parent;
	int error;

	error = pxa27x_keypad_matrix_key_parse(keypad);
	if (error) {
		dev_err(dev, "failed to parse matrix key\n");
		return error;
	}

	error = pxa27x_keypad_direct_key_parse(keypad);
	if (error) {
		dev_err(dev, "failed to parse direct key\n");
		return error;
	}

	error = pxa27x_keypad_rotary_parse(keypad);
	if (error) {
		dev_err(dev, "failed to parse rotary key\n");
		return error;
	}

	error = device_property_read_u32(dev, "marvell,debounce-interval",
					 &keypad->debounce_interval);
	if (error) {
		dev_err(dev, "failed to parse debounce-interval\n");
		return error;
	}

	/*
	 * The keycodes may not only includes matrix key but also the direct
	 * key or rotary key.
	 */
	input_dev->keycodemax = ARRAY_SIZE(keypad->keycodes);

	return 0;
}

static void pxa27x_keypad_scan_matrix(struct pxa27x_keypad *keypad)
{
	struct input_dev *input_dev = keypad->input_dev;
	int row, col, num_keys_pressed = 0;
	u32 new_state[MAX_MATRIX_KEY_COLS];
	u32 kpas = keypad_readl(KPAS);

	num_keys_pressed = KPAS_MUKP(kpas);

	memset(new_state, 0, sizeof(new_state));

	if (num_keys_pressed == 0)
		goto scan;

	if (num_keys_pressed == 1) {
		col = KPAS_CP(kpas);
		row = KPAS_RP(kpas);

		/* if invalid row/col, treat as no key pressed */
		if (col >= keypad->matrix_key_cols ||
		    row >= keypad->matrix_key_rows)
			goto scan;

		new_state[col] = BIT(row);
		goto scan;
	}

	if (num_keys_pressed > 1) {
		u32 kpasmkp0 = keypad_readl(KPASMKP0);
		u32 kpasmkp1 = keypad_readl(KPASMKP1);
		u32 kpasmkp2 = keypad_readl(KPASMKP2);
		u32 kpasmkp3 = keypad_readl(KPASMKP3);

		new_state[0] = kpasmkp0 & KPASMKP_MKC_MASK;
		new_state[1] = (kpasmkp0 >> 16) & KPASMKP_MKC_MASK;
		new_state[2] = kpasmkp1 & KPASMKP_MKC_MASK;
		new_state[3] = (kpasmkp1 >> 16) & KPASMKP_MKC_MASK;
		new_state[4] = kpasmkp2 & KPASMKP_MKC_MASK;
		new_state[5] = (kpasmkp2 >> 16) & KPASMKP_MKC_MASK;
		new_state[6] = kpasmkp3 & KPASMKP_MKC_MASK;
		new_state[7] = (kpasmkp3 >> 16) & KPASMKP_MKC_MASK;
	}
scan:
	for (col = 0; col < keypad->matrix_key_cols; col++) {
		u32 bits_changed;
		int code;

		bits_changed = keypad->matrix_key_state[col] ^ new_state[col];
		if (bits_changed == 0)
			continue;

		for (row = 0; row < keypad->matrix_key_rows; row++) {
			if ((bits_changed & BIT(row)) == 0)
				continue;

			code = MATRIX_SCAN_CODE(row, col, keypad->row_shift);

			input_event(input_dev, EV_MSC, MSC_SCAN, code);
			input_report_key(input_dev, keypad->keycodes[code],
					 new_state[col] & BIT(row));
		}
	}
	input_sync(input_dev);
	memcpy(keypad->matrix_key_state, new_state, sizeof(new_state));
}

#define DEFAULT_KPREC	(0x007f007f)

static inline int rotary_delta(u32 kprec)
{
	if (kprec & KPREC_OF0)
		return (kprec & 0xff) + 0x7f;
	else if (kprec & KPREC_UF0)
		return (kprec & 0xff) - 0x7f - 0xff;
	else
		return (kprec & 0xff) - 0x7f;
}

static void report_rotary_event(struct pxa27x_keypad *keypad, int r, int delta)
{
	struct pxa27x_keypad_rotary *encoder = &keypad->rotary[r];
	struct input_dev *dev = keypad->input_dev;

	if (!encoder->enabled || delta == 0)
		return;

	if (encoder->rel_code == -1) {
		int idx = delta > 0 ? 0 : 1;
		int code = MAX_MATRIX_KEY_NUM + 2 * r + idx;
		unsigned char keycode = encoder->key_codes[idx];

		/* simulate a press-n-release */
		input_event(dev, EV_MSC, MSC_SCAN, code);
		input_report_key(dev, keycode, 1);
		input_sync(dev);
		input_event(dev, EV_MSC, MSC_SCAN, code);
		input_report_key(dev, keycode, 0);
		input_sync(dev);
	} else {
		input_report_rel(dev, encoder->rel_code, delta);
		input_sync(dev);
	}
}

static void pxa27x_keypad_scan_rotary(struct pxa27x_keypad *keypad)
{
	u32 kprec;
	int i;

	/* read and reset to default count value */
	kprec = keypad_readl(KPREC);
	keypad_writel(KPREC, DEFAULT_KPREC);

	for (i = 0; i < MAX_ROTARY_ENCODERS; i++) {
		report_rotary_event(keypad, 0, rotary_delta(kprec));
		kprec >>= 16;
	}
}

static void pxa27x_keypad_scan_direct(struct pxa27x_keypad *keypad)
{
	struct input_dev *input_dev = keypad->input_dev;
	unsigned int new_state;
	u32 kpdk, bits_changed;
	int i;

	kpdk = keypad_readl(KPDK);

	if (keypad->rotary[0].enabled || keypad->rotary[1].enabled)
		pxa27x_keypad_scan_rotary(keypad);

	/*
	 * The KPDR_DK only output the key pin level, so it relates to board,
	 * and low level may be active.
	 */
	if (keypad->direct_key_low_active)
		new_state = ~KPDK_DK(kpdk) & keypad->direct_key_mask;
	else
		new_state = KPDK_DK(kpdk) & keypad->direct_key_mask;

	bits_changed = keypad->direct_key_state ^ new_state;

	if (bits_changed == 0)
		return;

	for (i = 0; i < keypad->direct_key_num; i++) {
		if (bits_changed & BIT(i)) {
			int code = MAX_MATRIX_KEY_NUM + i;

			input_event(input_dev, EV_MSC, MSC_SCAN, code);
			input_report_key(input_dev, keypad->keycodes[code],
					 new_state & BIT(i));
		}
	}
	input_sync(input_dev);
	keypad->direct_key_state = new_state;
}

static irqreturn_t pxa27x_keypad_irq_handler(int irq, void *dev_id)
{
	struct pxa27x_keypad *keypad = dev_id;
	unsigned long kpc = keypad_readl(KPC);

	if (kpc & KPC_DI)
		pxa27x_keypad_scan_direct(keypad);

	if (kpc & KPC_MI)
		pxa27x_keypad_scan_matrix(keypad);

	return IRQ_HANDLED;
}

static void pxa27x_keypad_config(struct pxa27x_keypad *keypad)
{
	unsigned int mask = 0, direct_key_num = 0;
	unsigned long kpc = 0;

	/* clear pending interrupt bit */
	keypad_readl(KPC);

	/* enable matrix keys with automatic scan */
	if (keypad->matrix_key_rows && keypad->matrix_key_cols) {
		kpc |= KPC_ASACT | KPC_MIE | KPC_ME | KPC_MS_ALL;
		kpc |= KPC_MKRN(keypad->matrix_key_rows) |
		       KPC_MKCN(keypad->matrix_key_cols);
	}

	/* enable rotary key, debounce interval same as direct keys */
	if (keypad->rotary[0].enabled) {
		mask |= 0x03;
		direct_key_num = 2;
		kpc |= KPC_REE0;
	}

	if (keypad->rotary[1].enabled) {
		mask |= 0x0c;
		direct_key_num = 4;
		kpc |= KPC_REE1;
	}

	if (keypad->direct_key_num > direct_key_num)
		direct_key_num = keypad->direct_key_num;

	/*
	 * Direct keys usage may not start from KP_DKIN0, check the platfrom
	 * mask data to config the specific.
	 */
	if (!keypad->direct_key_mask)
		keypad->direct_key_mask = GENMASK(direct_key_num - 1, 0) & ~mask;

	/* enable direct key */
	if (direct_key_num)
		kpc |= KPC_DE | KPC_DIE | KPC_DKN(direct_key_num);

	keypad_writel(KPC, kpc | KPC_RE_ZERO_DEB);
	keypad_writel(KPREC, DEFAULT_KPREC);
	keypad_writel(KPKDI, keypad->debounce_interval);
}

static int pxa27x_keypad_open(struct input_dev *dev)
{
	struct pxa27x_keypad *keypad = input_get_drvdata(dev);
	int ret;
	/* Enable unit clock */
	ret = clk_prepare_enable(keypad->clk);
	if (ret)
		return ret;

	pxa27x_keypad_config(keypad);

	return 0;
}

static void pxa27x_keypad_close(struct input_dev *dev)
{
	struct pxa27x_keypad *keypad = input_get_drvdata(dev);

	/* Disable clock unit */
	clk_disable_unprepare(keypad->clk);
}

static int pxa27x_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);

	/*
	 * If the keypad is used a wake up source, clock can not be disabled.
	 * Or it can not detect the key pressing.
	 */
	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(keypad->irq);
	else
		clk_disable_unprepare(keypad->clk);

	return 0;
}

static int pxa27x_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa27x_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;
	int error;

	/*
	 * If the keypad is used as wake up source, the clock is not turned
	 * off. So do not need configure it again.
	 */
	if (device_may_wakeup(&pdev->dev)) {
		disable_irq_wake(keypad->irq);
	} else {
		guard(mutex)(&input_dev->mutex);

		if (input_device_enabled(input_dev)) {
			/* Enable unit clock */
			error = clk_prepare_enable(keypad->clk);
			if (error)
				return error;

			pxa27x_keypad_config(keypad);
		}
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pxa27x_keypad_pm_ops,
				pxa27x_keypad_suspend, pxa27x_keypad_resume);

static int pxa27x_keypad_probe(struct platform_device *pdev)
{
	struct pxa27x_keypad *keypad;
	struct input_dev *input_dev;
	int irq;
	int error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;

	keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad),
			      GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	keypad->input_dev = input_dev;
	keypad->irq = irq;

	keypad->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(keypad->mmio_base))
		return PTR_ERR(keypad->mmio_base);

	keypad->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(keypad->clk)) {
		dev_err(&pdev->dev, "failed to get keypad clock\n");
		return PTR_ERR(keypad->clk);
	}

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->open = pxa27x_keypad_open;
	input_dev->close = pxa27x_keypad_close;
	input_dev->dev.parent = &pdev->dev;

	input_dev->keycode = keypad->keycodes;
	input_dev->keycodesize = sizeof(keypad->keycodes[0]);
	input_dev->keycodemax = ARRAY_SIZE(keypad->keycodes);

	input_set_drvdata(input_dev, keypad);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	error = pxa27x_keypad_parse_properties(keypad);
	if (error) {
		dev_err(&pdev->dev, "failed to parse keypad properties\n");
		return error;
	}

	error = devm_request_irq(&pdev->dev, irq, pxa27x_keypad_irq_handler,
				 0, pdev->name, keypad);
	if (error) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return error;
	}

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return error;
	}

	platform_set_drvdata(pdev, keypad);
	device_init_wakeup(&pdev->dev, 1);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pxa27x_keypad_dt_match[] = {
	{ .compatible = "marvell,pxa27x-keypad" },
	{},
};
MODULE_DEVICE_TABLE(of, pxa27x_keypad_dt_match);
#endif

static struct platform_driver pxa27x_keypad_driver = {
	.probe		= pxa27x_keypad_probe,
	.driver		= {
		.name	= "pxa27x-keypad",
		.of_match_table = of_match_ptr(pxa27x_keypad_dt_match),
		.pm	= pm_sleep_ptr(&pxa27x_keypad_pm_ops),
	},
};
module_platform_driver(pxa27x_keypad_driver);

MODULE_DESCRIPTION("PXA27x Keypad Controller Driver");
MODULE_LICENSE("GPL");
/* work with hotplug and coldplug */
MODULE_ALIAS("platform:pxa27x-keypad");
