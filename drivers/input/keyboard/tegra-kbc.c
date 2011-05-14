/*
 * Keyboard class input driver for the NVIDIA Tegra SoC internal matrix
 * keyboard controller
 *
 * Copyright (c) 2009-2011, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <mach/clk.h>
#include <mach/kbc.h>

#define KBC_MAX_DEBOUNCE_CNT	0x3ffu

/* KBC row scan time and delay for beginning the row scan. */
#define KBC_ROW_SCAN_TIME	16
#define KBC_ROW_SCAN_DLY	5

/* KBC uses a 32KHz clock so a cycle = 1/32Khz */
#define KBC_CYCLE_USEC	32

/* KBC Registers */

/* KBC Control Register */
#define KBC_CONTROL_0	0x0
#define KBC_FIFO_TH_CNT_SHIFT(cnt)	(cnt << 14)
#define KBC_DEBOUNCE_CNT_SHIFT(cnt)	(cnt << 4)
#define KBC_CONTROL_FIFO_CNT_INT_EN	(1 << 3)
#define KBC_CONTROL_KBC_EN		(1 << 0)

/* KBC Interrupt Register */
#define KBC_INT_0	0x4
#define KBC_INT_FIFO_CNT_INT_STATUS	(1 << 2)

#define KBC_ROW_CFG0_0	0x8
#define KBC_COL_CFG0_0	0x18
#define KBC_INIT_DLY_0	0x28
#define KBC_RPT_DLY_0	0x2c
#define KBC_KP_ENT0_0	0x30
#define KBC_KP_ENT1_0	0x34
#define KBC_ROW0_MASK_0	0x38

#define KBC_ROW_SHIFT	3

struct tegra_kbc {
	void __iomem *mmio;
	struct input_dev *idev;
	unsigned int irq;
	unsigned int wake_enable_rows;
	unsigned int wake_enable_cols;
	spinlock_t lock;
	unsigned int repoll_dly;
	unsigned long cp_dly_jiffies;
	bool use_fn_map;
	const struct tegra_kbc_platform_data *pdata;
	unsigned short keycode[KBC_MAX_KEY * 2];
	unsigned short current_keys[KBC_MAX_KPENT];
	unsigned int num_pressed_keys;
	struct timer_list timer;
	struct clk *clk;
};

static const u32 tegra_kbc_default_keymap[] = {
	KEY(0, 2, KEY_W),
	KEY(0, 3, KEY_S),
	KEY(0, 4, KEY_A),
	KEY(0, 5, KEY_Z),
	KEY(0, 7, KEY_FN),

	KEY(1, 7, KEY_LEFTMETA),

	KEY(2, 6, KEY_RIGHTALT),
	KEY(2, 7, KEY_LEFTALT),

	KEY(3, 0, KEY_5),
	KEY(3, 1, KEY_4),
	KEY(3, 2, KEY_R),
	KEY(3, 3, KEY_E),
	KEY(3, 4, KEY_F),
	KEY(3, 5, KEY_D),
	KEY(3, 6, KEY_X),

	KEY(4, 0, KEY_7),
	KEY(4, 1, KEY_6),
	KEY(4, 2, KEY_T),
	KEY(4, 3, KEY_H),
	KEY(4, 4, KEY_G),
	KEY(4, 5, KEY_V),
	KEY(4, 6, KEY_C),
	KEY(4, 7, KEY_SPACE),

	KEY(5, 0, KEY_9),
	KEY(5, 1, KEY_8),
	KEY(5, 2, KEY_U),
	KEY(5, 3, KEY_Y),
	KEY(5, 4, KEY_J),
	KEY(5, 5, KEY_N),
	KEY(5, 6, KEY_B),
	KEY(5, 7, KEY_BACKSLASH),

	KEY(6, 0, KEY_MINUS),
	KEY(6, 1, KEY_0),
	KEY(6, 2, KEY_O),
	KEY(6, 3, KEY_I),
	KEY(6, 4, KEY_L),
	KEY(6, 5, KEY_K),
	KEY(6, 6, KEY_COMMA),
	KEY(6, 7, KEY_M),

	KEY(7, 1, KEY_EQUAL),
	KEY(7, 2, KEY_RIGHTBRACE),
	KEY(7, 3, KEY_ENTER),
	KEY(7, 7, KEY_MENU),

	KEY(8, 4, KEY_RIGHTSHIFT),
	KEY(8, 5, KEY_LEFTSHIFT),

	KEY(9, 5, KEY_RIGHTCTRL),
	KEY(9, 7, KEY_LEFTCTRL),

	KEY(11, 0, KEY_LEFTBRACE),
	KEY(11, 1, KEY_P),
	KEY(11, 2, KEY_APOSTROPHE),
	KEY(11, 3, KEY_SEMICOLON),
	KEY(11, 4, KEY_SLASH),
	KEY(11, 5, KEY_DOT),

	KEY(12, 0, KEY_F10),
	KEY(12, 1, KEY_F9),
	KEY(12, 2, KEY_BACKSPACE),
	KEY(12, 3, KEY_3),
	KEY(12, 4, KEY_2),
	KEY(12, 5, KEY_UP),
	KEY(12, 6, KEY_PRINT),
	KEY(12, 7, KEY_PAUSE),

	KEY(13, 0, KEY_INSERT),
	KEY(13, 1, KEY_DELETE),
	KEY(13, 3, KEY_PAGEUP),
	KEY(13, 4, KEY_PAGEDOWN),
	KEY(13, 5, KEY_RIGHT),
	KEY(13, 6, KEY_DOWN),
	KEY(13, 7, KEY_LEFT),

	KEY(14, 0, KEY_F11),
	KEY(14, 1, KEY_F12),
	KEY(14, 2, KEY_F8),
	KEY(14, 3, KEY_Q),
	KEY(14, 4, KEY_F4),
	KEY(14, 5, KEY_F3),
	KEY(14, 6, KEY_1),
	KEY(14, 7, KEY_F7),

	KEY(15, 0, KEY_ESC),
	KEY(15, 1, KEY_GRAVE),
	KEY(15, 2, KEY_F5),
	KEY(15, 3, KEY_TAB),
	KEY(15, 4, KEY_F1),
	KEY(15, 5, KEY_F2),
	KEY(15, 6, KEY_CAPSLOCK),
	KEY(15, 7, KEY_F6),

	/* Software Handled Function Keys */
	KEY(20, 0, KEY_KP7),

	KEY(21, 0, KEY_KP9),
	KEY(21, 1, KEY_KP8),
	KEY(21, 2, KEY_KP4),
	KEY(21, 4, KEY_KP1),

	KEY(22, 1, KEY_KPSLASH),
	KEY(22, 2, KEY_KP6),
	KEY(22, 3, KEY_KP5),
	KEY(22, 4, KEY_KP3),
	KEY(22, 5, KEY_KP2),
	KEY(22, 7, KEY_KP0),

	KEY(27, 1, KEY_KPASTERISK),
	KEY(27, 3, KEY_KPMINUS),
	KEY(27, 4, KEY_KPPLUS),
	KEY(27, 5, KEY_KPDOT),

	KEY(28, 5, KEY_VOLUMEUP),

	KEY(29, 3, KEY_HOME),
	KEY(29, 4, KEY_END),
	KEY(29, 5, KEY_BRIGHTNESSDOWN),
	KEY(29, 6, KEY_VOLUMEDOWN),
	KEY(29, 7, KEY_BRIGHTNESSUP),

	KEY(30, 0, KEY_NUMLOCK),
	KEY(30, 1, KEY_SCROLLLOCK),
	KEY(30, 2, KEY_MUTE),

	KEY(31, 4, KEY_HELP),
};

static const struct matrix_keymap_data tegra_kbc_default_keymap_data = {
	.keymap		= tegra_kbc_default_keymap,
	.keymap_size	= ARRAY_SIZE(tegra_kbc_default_keymap),
};

static void tegra_kbc_report_released_keys(struct input_dev *input,
					   unsigned short old_keycodes[],
					   unsigned int old_num_keys,
					   unsigned short new_keycodes[],
					   unsigned int new_num_keys)
{
	unsigned int i, j;

	for (i = 0; i < old_num_keys; i++) {
		for (j = 0; j < new_num_keys; j++)
			if (old_keycodes[i] == new_keycodes[j])
				break;

		if (j == new_num_keys)
			input_report_key(input, old_keycodes[i], 0);
	}
}

static void tegra_kbc_report_pressed_keys(struct input_dev *input,
					  unsigned char scancodes[],
					  unsigned short keycodes[],
					  unsigned int num_pressed_keys)
{
	unsigned int i;

	for (i = 0; i < num_pressed_keys; i++) {
		input_event(input, EV_MSC, MSC_SCAN, scancodes[i]);
		input_report_key(input, keycodes[i], 1);
	}
}

static void tegra_kbc_report_keys(struct tegra_kbc *kbc)
{
	unsigned char scancodes[KBC_MAX_KPENT];
	unsigned short keycodes[KBC_MAX_KPENT];
	u32 val = 0;
	unsigned int i;
	unsigned int num_down = 0;
	unsigned long flags;
	bool fn_keypress = false;

	spin_lock_irqsave(&kbc->lock, flags);
	for (i = 0; i < KBC_MAX_KPENT; i++) {
		if ((i % 4) == 0)
			val = readl(kbc->mmio + KBC_KP_ENT0_0 + i);

		if (val & 0x80) {
			unsigned int col = val & 0x07;
			unsigned int row = (val >> 3) & 0x0f;
			unsigned char scancode =
				MATRIX_SCAN_CODE(row, col, KBC_ROW_SHIFT);

			scancodes[num_down] = scancode;
			keycodes[num_down] = kbc->keycode[scancode];
			/* If driver uses Fn map, do not report the Fn key. */
			if ((keycodes[num_down] == KEY_FN) && kbc->use_fn_map)
				fn_keypress = true;
			else
				num_down++;
		}

		val >>= 8;
	}

	/*
	 * If the platform uses Fn keymaps, translate keys on a Fn keypress.
	 * Function keycodes are KBC_MAX_KEY apart from the plain keycodes.
	 */
	if (fn_keypress) {
		for (i = 0; i < num_down; i++) {
			scancodes[i] += KBC_MAX_KEY;
			keycodes[i] = kbc->keycode[scancodes[i]];
		}
	}

	spin_unlock_irqrestore(&kbc->lock, flags);

	tegra_kbc_report_released_keys(kbc->idev,
				       kbc->current_keys, kbc->num_pressed_keys,
				       keycodes, num_down);
	tegra_kbc_report_pressed_keys(kbc->idev, scancodes, keycodes, num_down);
	input_sync(kbc->idev);

	memcpy(kbc->current_keys, keycodes, sizeof(kbc->current_keys));
	kbc->num_pressed_keys = num_down;
}

static void tegra_kbc_keypress_timer(unsigned long data)
{
	struct tegra_kbc *kbc = (struct tegra_kbc *)data;
	unsigned long flags;
	u32 val;
	unsigned int i;

	val = (readl(kbc->mmio + KBC_INT_0) >> 4) & 0xf;
	if (val) {
		unsigned long dly;

		tegra_kbc_report_keys(kbc);

		/*
		 * If more than one keys are pressed we need not wait
		 * for the repoll delay.
		 */
		dly = (val == 1) ? kbc->repoll_dly : 1;
		mod_timer(&kbc->timer, jiffies + msecs_to_jiffies(dly));
	} else {
		/* Release any pressed keys and exit the polling loop */
		for (i = 0; i < kbc->num_pressed_keys; i++)
			input_report_key(kbc->idev, kbc->current_keys[i], 0);
		input_sync(kbc->idev);

		kbc->num_pressed_keys = 0;

		/* All keys are released so enable the keypress interrupt */
		spin_lock_irqsave(&kbc->lock, flags);
		val = readl(kbc->mmio + KBC_CONTROL_0);
		val |= KBC_CONTROL_FIFO_CNT_INT_EN;
		writel(val, kbc->mmio + KBC_CONTROL_0);
		spin_unlock_irqrestore(&kbc->lock, flags);
	}
}

static irqreturn_t tegra_kbc_isr(int irq, void *args)
{
	struct tegra_kbc *kbc = args;
	u32 val, ctl;

	/*
	 * Until all keys are released, defer further processing to
	 * the polling loop in tegra_kbc_keypress_timer
	 */
	ctl = readl(kbc->mmio + KBC_CONTROL_0);
	ctl &= ~KBC_CONTROL_FIFO_CNT_INT_EN;
	writel(ctl, kbc->mmio + KBC_CONTROL_0);

	/*
	 * Quickly bail out & reenable interrupts if the fifo threshold
	 * count interrupt wasn't the interrupt source
	 */
	val = readl(kbc->mmio + KBC_INT_0);
	writel(val, kbc->mmio + KBC_INT_0);

	if (val & KBC_INT_FIFO_CNT_INT_STATUS) {
		/*
		 * Schedule timer to run when hardware is in continuous
		 * polling mode.
		 */
		mod_timer(&kbc->timer, jiffies + kbc->cp_dly_jiffies);
	} else {
		ctl |= KBC_CONTROL_FIFO_CNT_INT_EN;
		writel(ctl, kbc->mmio + KBC_CONTROL_0);
	}

	return IRQ_HANDLED;
}

static void tegra_kbc_setup_wakekeys(struct tegra_kbc *kbc, bool filter)
{
	const struct tegra_kbc_platform_data *pdata = kbc->pdata;
	int i;
	unsigned int rst_val;

	BUG_ON(pdata->wake_cnt > KBC_MAX_KEY);
	rst_val = (filter && pdata->wake_cnt) ? ~0 : 0;

	for (i = 0; i < KBC_MAX_ROW; i++)
		writel(rst_val, kbc->mmio + KBC_ROW0_MASK_0 + i * 4);

	if (filter) {
		for (i = 0; i < pdata->wake_cnt; i++) {
			u32 val, addr;
			addr = pdata->wake_cfg[i].row * 4 + KBC_ROW0_MASK_0;
			val = readl(kbc->mmio + addr);
			val &= ~(1 << pdata->wake_cfg[i].col);
			writel(val, kbc->mmio + addr);
		}
	}
}

static void tegra_kbc_config_pins(struct tegra_kbc *kbc)
{
	const struct tegra_kbc_platform_data *pdata = kbc->pdata;
	int i;

	for (i = 0; i < KBC_MAX_GPIO; i++) {
		u32 r_shft = 5 * (i % 6);
		u32 c_shft = 4 * (i % 8);
		u32 r_mask = 0x1f << r_shft;
		u32 c_mask = 0x0f << c_shft;
		u32 r_offs = (i / 6) * 4 + KBC_ROW_CFG0_0;
		u32 c_offs = (i / 8) * 4 + KBC_COL_CFG0_0;
		u32 row_cfg = readl(kbc->mmio + r_offs);
		u32 col_cfg = readl(kbc->mmio + c_offs);

		row_cfg &= ~r_mask;
		col_cfg &= ~c_mask;

		if (pdata->pin_cfg[i].is_row)
			row_cfg |= ((pdata->pin_cfg[i].num << 1) | 1) << r_shft;
		else
			col_cfg |= ((pdata->pin_cfg[i].num << 1) | 1) << c_shft;

		writel(row_cfg, kbc->mmio + r_offs);
		writel(col_cfg, kbc->mmio + c_offs);
	}
}

static int tegra_kbc_start(struct tegra_kbc *kbc)
{
	const struct tegra_kbc_platform_data *pdata = kbc->pdata;
	unsigned long flags;
	unsigned int debounce_cnt;
	u32 val = 0;

	clk_enable(kbc->clk);

	/* Reset the KBC controller to clear all previous status.*/
	tegra_periph_reset_assert(kbc->clk);
	udelay(100);
	tegra_periph_reset_deassert(kbc->clk);
	udelay(100);

	tegra_kbc_config_pins(kbc);
	tegra_kbc_setup_wakekeys(kbc, false);

	writel(pdata->repeat_cnt, kbc->mmio + KBC_RPT_DLY_0);

	/* Keyboard debounce count is maximum of 12 bits. */
	debounce_cnt = min(pdata->debounce_cnt, KBC_MAX_DEBOUNCE_CNT);
	val = KBC_DEBOUNCE_CNT_SHIFT(debounce_cnt);
	val |= KBC_FIFO_TH_CNT_SHIFT(1); /* set fifo interrupt threshold to 1 */
	val |= KBC_CONTROL_FIFO_CNT_INT_EN;  /* interrupt on FIFO threshold */
	val |= KBC_CONTROL_KBC_EN;     /* enable */
	writel(val, kbc->mmio + KBC_CONTROL_0);

	/*
	 * Compute the delay(ns) from interrupt mode to continuous polling
	 * mode so the timer routine is scheduled appropriately.
	 */
	val = readl(kbc->mmio + KBC_INIT_DLY_0);
	kbc->cp_dly_jiffies = usecs_to_jiffies((val & 0xfffff) * 32);

	kbc->num_pressed_keys = 0;

	/*
	 * Atomically clear out any remaining entries in the key FIFO
	 * and enable keyboard interrupts.
	 */
	spin_lock_irqsave(&kbc->lock, flags);
	while (1) {
		val = readl(kbc->mmio + KBC_INT_0);
		val >>= 4;
		if (!val)
			break;

		val = readl(kbc->mmio + KBC_KP_ENT0_0);
		val = readl(kbc->mmio + KBC_KP_ENT1_0);
	}
	writel(0x7, kbc->mmio + KBC_INT_0);
	spin_unlock_irqrestore(&kbc->lock, flags);

	enable_irq(kbc->irq);

	return 0;
}

static void tegra_kbc_stop(struct tegra_kbc *kbc)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&kbc->lock, flags);
	val = readl(kbc->mmio + KBC_CONTROL_0);
	val &= ~1;
	writel(val, kbc->mmio + KBC_CONTROL_0);
	spin_unlock_irqrestore(&kbc->lock, flags);

	disable_irq(kbc->irq);
	del_timer_sync(&kbc->timer);

	clk_disable(kbc->clk);
}

static int tegra_kbc_open(struct input_dev *dev)
{
	struct tegra_kbc *kbc = input_get_drvdata(dev);

	return tegra_kbc_start(kbc);
}

static void tegra_kbc_close(struct input_dev *dev)
{
	struct tegra_kbc *kbc = input_get_drvdata(dev);

	return tegra_kbc_stop(kbc);
}

static bool __devinit
tegra_kbc_check_pin_cfg(const struct tegra_kbc_platform_data *pdata,
			struct device *dev, unsigned int *num_rows)
{
	int i;

	*num_rows = 0;

	for (i = 0; i < KBC_MAX_GPIO; i++) {
		const struct tegra_kbc_pin_cfg *pin_cfg = &pdata->pin_cfg[i];

		if (pin_cfg->is_row) {
			if (pin_cfg->num >= KBC_MAX_ROW) {
				dev_err(dev,
					"pin_cfg[%d]: invalid row number %d\n",
					i, pin_cfg->num);
				return false;
			}
			(*num_rows)++;
		} else {
			if (pin_cfg->num >= KBC_MAX_COL) {
				dev_err(dev,
					"pin_cfg[%d]: invalid column number %d\n",
					i, pin_cfg->num);
				return false;
			}
		}
	}

	return true;
}

static int __devinit tegra_kbc_probe(struct platform_device *pdev)
{
	const struct tegra_kbc_platform_data *pdata = pdev->dev.platform_data;
	const struct matrix_keymap_data *keymap_data;
	struct tegra_kbc *kbc;
	struct input_dev *input_dev;
	struct resource *res;
	int irq;
	int err;
	int i;
	int num_rows = 0;
	unsigned int debounce_cnt;
	unsigned int scan_time_rows;

	if (!pdata)
		return -EINVAL;

	if (!tegra_kbc_check_pin_cfg(pdata, &pdev->dev, &num_rows))
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		return -ENXIO;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get keyboard IRQ\n");
		return -ENXIO;
	}

	kbc = kzalloc(sizeof(*kbc), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!kbc || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	kbc->pdata = pdata;
	kbc->idev = input_dev;
	kbc->irq = irq;
	spin_lock_init(&kbc->lock);
	setup_timer(&kbc->timer, tegra_kbc_keypress_timer, (unsigned long)kbc);

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		err = -EBUSY;
		goto err_free_mem;
	}

	kbc->mmio = ioremap(res->start, resource_size(res));
	if (!kbc->mmio) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		err = -ENXIO;
		goto err_free_mem_region;
	}

	kbc->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(kbc->clk)) {
		dev_err(&pdev->dev, "failed to get keyboard clock\n");
		err = PTR_ERR(kbc->clk);
		goto err_iounmap;
	}

	kbc->wake_enable_rows = 0;
	kbc->wake_enable_cols = 0;
	for (i = 0; i < pdata->wake_cnt; i++) {
		kbc->wake_enable_rows |= (1 << pdata->wake_cfg[i].row);
		kbc->wake_enable_cols |= (1 << pdata->wake_cfg[i].col);
	}

	/*
	 * The time delay between two consecutive reads of the FIFO is
	 * the sum of the repeat time and the time taken for scanning
	 * the rows. There is an additional delay before the row scanning
	 * starts. The repoll delay is computed in milliseconds.
	 */
	debounce_cnt = min(pdata->debounce_cnt, KBC_MAX_DEBOUNCE_CNT);
	scan_time_rows = (KBC_ROW_SCAN_TIME + debounce_cnt) * num_rows;
	kbc->repoll_dly = KBC_ROW_SCAN_DLY + scan_time_rows + pdata->repeat_cnt;
	kbc->repoll_dly = ((kbc->repoll_dly * KBC_CYCLE_USEC) + 999) / 1000;

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->dev.parent = &pdev->dev;
	input_dev->open = tegra_kbc_open;
	input_dev->close = tegra_kbc_close;

	input_set_drvdata(input_dev, kbc);

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	input_dev->keycode = kbc->keycode;
	input_dev->keycodesize = sizeof(kbc->keycode[0]);
	input_dev->keycodemax = KBC_MAX_KEY;
	if (pdata->use_fn_map)
		input_dev->keycodemax *= 2;

	kbc->use_fn_map = pdata->use_fn_map;
	keymap_data = pdata->keymap_data ?: &tegra_kbc_default_keymap_data;
	matrix_keypad_build_keymap(keymap_data, KBC_ROW_SHIFT,
				   input_dev->keycode, input_dev->keybit);

	err = request_irq(kbc->irq, tegra_kbc_isr, IRQF_TRIGGER_HIGH,
			  pdev->name, kbc);
	if (err) {
		dev_err(&pdev->dev, "failed to request keyboard IRQ\n");
		goto err_put_clk;
	}

	disable_irq(kbc->irq);

	err = input_register_device(kbc->idev);
	if (err) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto err_free_irq;
	}

	platform_set_drvdata(pdev, kbc);
	device_init_wakeup(&pdev->dev, pdata->wakeup);

	return 0;

err_free_irq:
	free_irq(kbc->irq, pdev);
err_put_clk:
	clk_put(kbc->clk);
err_iounmap:
	iounmap(kbc->mmio);
err_free_mem_region:
	release_mem_region(res->start, resource_size(res));
err_free_mem:
	input_free_device(kbc->idev);
	kfree(kbc);

	return err;
}

static int __devexit tegra_kbc_remove(struct platform_device *pdev)
{
	struct tegra_kbc *kbc = platform_get_drvdata(pdev);
	struct resource *res;

	free_irq(kbc->irq, pdev);
	clk_put(kbc->clk);

	input_unregister_device(kbc->idev);
	iounmap(kbc->mmio);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	kfree(kbc);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_kbc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_kbc *kbc = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev)) {
		tegra_kbc_setup_wakekeys(kbc, true);
		enable_irq_wake(kbc->irq);
		/* Forcefully clear the interrupt status */
		writel(0x7, kbc->mmio + KBC_INT_0);
		msleep(30);
	} else {
		mutex_lock(&kbc->idev->mutex);
		if (kbc->idev->users)
			tegra_kbc_stop(kbc);
		mutex_unlock(&kbc->idev->mutex);
	}

	return 0;
}

static int tegra_kbc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_kbc *kbc = platform_get_drvdata(pdev);
	int err = 0;

	if (device_may_wakeup(&pdev->dev)) {
		disable_irq_wake(kbc->irq);
		tegra_kbc_setup_wakekeys(kbc, false);
	} else {
		mutex_lock(&kbc->idev->mutex);
		if (kbc->idev->users)
			err = tegra_kbc_start(kbc);
		mutex_unlock(&kbc->idev->mutex);
	}

	return err;
}
#endif

static SIMPLE_DEV_PM_OPS(tegra_kbc_pm_ops, tegra_kbc_suspend, tegra_kbc_resume);

static struct platform_driver tegra_kbc_driver = {
	.probe		= tegra_kbc_probe,
	.remove		= __devexit_p(tegra_kbc_remove),
	.driver	= {
		.name	= "tegra-kbc",
		.owner  = THIS_MODULE,
		.pm	= &tegra_kbc_pm_ops,
	},
};

static void __exit tegra_kbc_exit(void)
{
	platform_driver_unregister(&tegra_kbc_driver);
}
module_exit(tegra_kbc_exit);

static int __init tegra_kbc_init(void)
{
	return platform_driver_register(&tegra_kbc_driver);
}
module_init(tegra_kbc_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rakesh Iyer <riyer@nvidia.com>");
MODULE_DESCRIPTION("Tegra matrix keyboard controller driver");
MODULE_ALIAS("platform:tegra-kbc");
