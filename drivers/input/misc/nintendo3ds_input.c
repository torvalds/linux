/*
 *  nintendo3ds_input.c
 *
 *  Copyright (C) 2015 Sergi Granell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/font.h>

#include <asm/io.h>

/***** Drawing *****/

// 3DS memory layout
#define VRAM_BASE     (0x18000000)
#define VRAM_SIZE     (0x00600000)
#define AXI_WRAM_BASE (0x1FF80000)
#define AXI_WRAM_SIZE (0x00080000)
#define FCRAM_BASE    (0x20000000)
#define FCRAM_SIZE    (0x08000000)

#define SCREEN_TOP_W  (400)
#define SCREEN_BOT_W  (320)
#define SCREEN_TOP_H  (240)
#define SCREEN_BOT_H  (240)

#define LCD_FB_PDC0           (0x10400400)
#define LCD_FB_PDC1           (0x10400500)
#define LCD_FB_PDC_SIZE       (0x100)
#define LCD_FB_A_ADDR_OFFSET  (0x68)
#define LCD_FB_B_ADDR_OFFSET  (0x94)
#define FB_TOP_SIZE           (SCREEN_TOP_W*SCREEN_TOP_H*3)
#define FB_BOT_SIZE           (SCREEN_BOT_W*SCREEN_BOT_H*3)
#define FB_TOP_LEFT1          (VRAM_BASE)
#define FB_TOP_LEFT2          (FB_TOP_LEFT1  + FB_TOP_SIZE)
#define FB_TOP_RIGHT1         (FB_TOP_LEFT2  + FB_TOP_SIZE)
#define FB_TOP_RIGHT2         (FB_TOP_RIGHT1 + FB_TOP_SIZE)
#define FB_BOT_1              (FB_TOP_RIGHT2 + FB_TOP_SIZE)
#define FB_BOT_2              (FB_BOT_1      + FB_BOT_SIZE)

#define RED		0xFF0000
#define GREEN		0x00FF00
#define BLUE		0x0000FF
#define CYAN		0x00FFFF
#define PINK		0xFF00FF
#define YELLOW		0xFFFF00
#define BLACK		0x000000
#define GREY		0x808080
#define WHITE		0xFFFFFF
#define ORANGE		0xFF9900
#define LIGHT_GREEN	0x00CC00
#define PURPLE		0x660033

static void map_lcd_bot_fb(void)
{
	u8 __iomem *lcd_fb_pdc1_base;

	if (request_mem_region(LCD_FB_PDC1, LCD_FB_PDC_SIZE, "N3DS_LCD_FB_PDC1")) {
		lcd_fb_pdc1_base = ioremap_nocache(LCD_FB_PDC1, LCD_FB_PDC_SIZE);

		printk("LCD_FB_PDC1 mapped to: %p - %p\n", lcd_fb_pdc1_base,
			lcd_fb_pdc1_base + LCD_FB_PDC_SIZE);
	} else {
		printk("LCD_FB_PDC1 region not available.\n");
		return;
	}

	iowrite32((SCREEN_BOT_H<<16) | SCREEN_BOT_W, lcd_fb_pdc1_base + 0x5C);
	iowrite32(FB_BOT_1, lcd_fb_pdc1_base + 0x68);
	iowrite32(FB_BOT_2, lcd_fb_pdc1_base + 0x6C);
	iowrite32(0b000001, lcd_fb_pdc1_base + 0x70);
	iowrite32(0, lcd_fb_pdc1_base + 0x78);
	iowrite32(SCREEN_BOT_H*3, lcd_fb_pdc1_base + 0x90);

	iounmap(lcd_fb_pdc1_base);
	release_mem_region(LCD_FB_PDC1, LCD_FB_PDC_SIZE);
}

static inline void fbbot_draw_pixel(void __iomem *fb_base, int x, int y, unsigned int color)
{
	u8 __iomem *dst = (u8 *)fb_base + ((SCREEN_BOT_H - y - 1) + x * SCREEN_BOT_H) * 3;
	iowrite8((color>>0 ) & 0xFF, dst + 0);
	iowrite8((color>>8 ) & 0xFF, dst + 1);
	iowrite8((color>>16) & 0xFF, dst + 2);
}

static inline void fbbot_draw_fillrect(void __iomem *fb_base, int x, int y, int w, int h, unsigned int color)
{
	int i, j;
	for (i = 0; i < h; i++)
		for (j = 0; j < w; j++)
			fbbot_draw_pixel(fb_base, x + j, y + i, color);
}

static inline void fbbot_clear_screen(void __iomem *fb_base, unsigned int color)
{
	fbbot_draw_fillrect(fb_base, 0, 0, SCREEN_BOT_W, SCREEN_BOT_H, color);
}

static void fbbot_draw_char(void __iomem *fb_base, const struct font_desc *font, int x, int y, unsigned int color, char c)
{
	int i, j;
	const u8 *src;

	src = font->data + c * font->height;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++) {
			fbbot_draw_pixel(fb_base, x+j, y+i, BLACK);
			if ((*src & (128 >> j)))
				fbbot_draw_pixel(fb_base, x+j, y+i, color);
		}
		src++;
	}
}

static int fbbot_draw_text(void __iomem *fb_base, const struct font_desc *font, int x, int y, unsigned int color, const char *text)
{
	char c;
	int sx = x;

	if (!text)
		return 0;

	while ((c = *text++)) {
		if (c == '\n') {
			x = sx;
			y += font->height;
		} else if (c == ' ') {
			x += font->width;
		} else if(c == '\t') {
			x += 4 * font->width;
		} else {
			fbbot_draw_fillrect(fb_base, x, y, font->width, font->height, BLACK);
			fbbot_draw_char(fb_base, font, x, y, color, c);
			x += font->width;
		}
	}

	return x - sx;
}

static void fbbot_draw_textf(void __iomem *fb_base, const struct font_desc *font, int x, int y, unsigned int color, const char *text, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, text);
	vsnprintf(buffer, 256, text, args);
	fbbot_draw_text(fb_base,font, x, y, color, buffer);
	va_end(args);
}

/***** Virtual keyboard *****/

#define VKB_ROWS (5)
#define VKB_COLS (14)

static const char *vkb_map_ascii[VKB_ROWS][VKB_COLS] = {
	{"`   ", "1",     "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "0",  "-",   "=",    "Del"  },
	{"TAB ", "Q",     "W",  "E",  "R",  "T",  "Y",  "U",  "I",  "O",  "P",  "{",   "}",     "\\"  },
	{"CAPS", "A",     "S",  "D",  "F",  "G",  "H",  "J",  "K",  "L",  ":",  "´",   "Enter", NULL  },
	{"SHFT", "Z",     "X",  "C",  "V",  "B",  "N",  "M",  ",",  ".",  "/",  "SHFT", NULL,   NULL  },
	{"CTRL", "SPACE", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,   NULL,   "CTRL"}
};


static const char vkb_map_keys[VKB_ROWS][VKB_COLS] = {
	{KEY_GRAVE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE},
	{KEY_TAB, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH},
	{KEY_CAPSLOCK, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_ENTER, 0},
	{KEY_LEFTSHIFT, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_RIGHTSHIFT, 0, 0},
	{KEY_LEFTCTRL, KEY_SPACE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_RIGHTCTRL}
};

struct vkb_ctx_t {
	int curx;
	int cury;
};

static void vkb_init(struct vkb_ctx_t *vkb)
{
	vkb->curx = VKB_COLS/2;
	vkb->cury = VKB_ROWS/2;
}

static void fbbot_draw_vkb(void __iomem *fb_base, const struct font_desc *font, int x, int y, const struct vkb_ctx_t *vkb)
{
	int i, j;
	int dx = x;
	int dy = y;

	for (i = 0; i < VKB_ROWS; i++) {
		for (j = 0; j < VKB_COLS; j++) {
			if (vkb_map_ascii[i][j]) {
				dx += fbbot_draw_text(fb_base, font, dx, dy,
					(vkb->curx == j && vkb->cury == i) ? YELLOW : WHITE,
					vkb_map_ascii[i][j]) + font->width;
			}
		}
		dx = x;
		dy += font->height;
	}
}

/***** Buttons *****/

/* We poll keys - msecs */
#define POLL_INTERVAL_DEFAULT	50

#define HID_INPUT_PA   (0x10146000)
#define HID_INPUT_SIZE (4)

#define BUTTON_A      (1 << 0)
#define BUTTON_B      (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START  (1 << 3)
#define BUTTON_RIGHT  (1 << 4)
#define BUTTON_LEFT   (1 << 5)
#define BUTTON_UP     (1 << 6)
#define BUTTON_DOWN   (1 << 7)
#define BUTTON_R1     (1 << 8)
#define BUTTON_L1     (1 << 9)
#define BUTTON_X      (1 << 10)
#define BUTTON_Y      (1 << 11)

#define BUTTON_PRESSED(b, m) (~(b) & (m))
#define BUTTON_CHANGED(b, o, m) ((b ^ o) & (m))

/*static const struct {
	unsigned int button;
	unsigned int code;
} button_map[] = {
	{BUTTON_A,	KEY_L},
	{BUTTON_B,	KEY_S},
	{BUTTON_Y,	KEY_BACKSPACE},
	{BUTTON_X,	KEY_SPACE},
	{BUTTON_START,	KEY_ENTER},
	{BUTTON_SELECT,	KEY_TAB},
	{BUTTON_UP,	KEY_UP},
	{BUTTON_DOWN,	KEY_DOWN},
	{BUTTON_RIGHT,	KEY_RIGHT},
	{BUTTON_LEFT,	KEY_LEFT},
	{BUTTON_L1,	KEY_HOME},
	{BUTTON_R1,	KEY_END}
};*/

struct nintendo3ds_input_dev {
	struct input_polled_dev *pdev;
	void __iomem *hid_input;
	void __iomem *fbbot;
	unsigned int old_buttons;
	const struct font_desc *font;
	struct vkb_ctx_t vkb;
};

static void nintendo3ds_input_poll(struct input_polled_dev *pdev)
{
	unsigned int buttons;
	struct nintendo3ds_input_dev *n3ds_input_dev = pdev->private;
	struct vkb_ctx_t *vkb = &n3ds_input_dev->vkb;
	struct input_dev *idev = pdev->input;

	buttons = ioread32(n3ds_input_dev->hid_input);

	if (buttons ^ n3ds_input_dev->old_buttons) {

		/* Crappy code (will change it soon) */
		if (BUTTON_CHANGED(buttons, n3ds_input_dev->old_buttons, BUTTON_A)) {
				input_report_key(idev, vkb_map_keys[vkb->cury][vkb->curx],
					BUTTON_PRESSED(buttons, BUTTON_A));
				input_sync(idev);
		} else if (BUTTON_CHANGED(buttons, n3ds_input_dev->old_buttons, BUTTON_START)) {
				input_report_key(idev, KEY_ENTER,
					BUTTON_PRESSED(buttons, BUTTON_START));
				input_sync(idev);
		} else if (BUTTON_CHANGED(buttons, n3ds_input_dev->old_buttons, BUTTON_B)) {
				input_report_key(idev, KEY_BACKSPACE,
					BUTTON_PRESSED(buttons, BUTTON_B));
				input_sync(idev);
		} else if (BUTTON_CHANGED(buttons, n3ds_input_dev->old_buttons, BUTTON_X)) {
				input_report_key(idev, KEY_SPACE,
					BUTTON_PRESSED(buttons, BUTTON_X));
				input_sync(idev);
		} else if (BUTTON_CHANGED(buttons, n3ds_input_dev->old_buttons, BUTTON_R1)) {
				input_report_key(idev, KEY_DOWN,
					BUTTON_PRESSED(buttons, BUTTON_R1));
				input_sync(idev);
		} else if (BUTTON_CHANGED(buttons, n3ds_input_dev->old_buttons, BUTTON_L1)) {
				input_report_key(idev, KEY_UP,
					BUTTON_PRESSED(buttons, BUTTON_L1));
				input_sync(idev);
		} else {
			if (BUTTON_PRESSED(buttons, BUTTON_UP)) {
				if (--vkb->cury < 0)
					vkb->cury = 0;
			} else if (BUTTON_PRESSED(buttons, BUTTON_DOWN)) {
				if (++vkb->cury > VKB_ROWS-1)
					vkb->cury = VKB_ROWS - 1;
			} else if (BUTTON_PRESSED(buttons, BUTTON_LEFT)) {
				if (--vkb->curx < 0)
					vkb->curx = 0;
			} else if (BUTTON_PRESSED(buttons, BUTTON_RIGHT)) {
				if (++vkb->curx > VKB_COLS-1)
					vkb->curx = VKB_COLS - 1;
			}

			fbbot_draw_vkb(n3ds_input_dev->fbbot, n3ds_input_dev->font, 0, 0, &n3ds_input_dev->vkb);
		}
	}

	n3ds_input_dev->old_buttons = buttons;
}

/*static void nintendo3ds_input_poll(struct input_polled_dev *pdev)
{
	int i;
	unsigned int buttons;
	struct nintendo3ds_input_dev *n3ds_input_dev = pdev->private;
	struct input_dev *idev = pdev->input;

	buttons = ioread32(n3ds_input_dev->hid_input);

	if (buttons ^ n3ds_input_dev->old_buttons) {
		for (i = 0; i < sizeof(button_map)/sizeof(*button_map); i++) {
			if (BUTTON_CHANGED(buttons, n3ds_input_dev->old_buttons,
				button_map[i].button)) {
				input_report_key(idev, button_map[i].code,
					BUTTON_PRESSED(buttons, button_map[i].button));
			}
		}
		input_sync(idev);
	}

	n3ds_input_dev->old_buttons = buttons;
}*/

static int nintendo3ds_input_probe(struct platform_device *plat_dev)
{
	int i, j;
	int error;
	struct nintendo3ds_input_dev *n3ds_input_dev;
	struct input_polled_dev *pdev;
	struct input_dev *idev;
	void *hid_input;
	void *fbbot;

	n3ds_input_dev = kzalloc(sizeof(*n3ds_input_dev), GFP_KERNEL);
	if (!n3ds_input_dev) {
		error = -ENOMEM;
		goto err_alloc_n3ds_input_dev;
	}

	/* Try to map HID_input */
	if (request_mem_region(HID_INPUT_PA, HID_INPUT_SIZE, "N3DS_HID_INPUT")) {
		hid_input = ioremap_nocache(HID_INPUT_PA, HID_INPUT_SIZE);

		printk("HID_INPUT mapped to: %p - %p\n", hid_input,
			hid_input + HID_INPUT_SIZE);
	} else {
		printk("HID_INPUT region not available.\n");
		error = -ENOMEM;
		goto err_hidmem;
	}

	/* Map bottom screen FB */
	if (request_mem_region(FB_BOT_1, FB_BOT_SIZE, "N3DS_BOT_FB")) {
		fbbot = ioremap_nocache(FB_BOT_1, FB_BOT_SIZE);

		printk("Bottom fb mapped to: %p - %p\n", fbbot,
			fbbot + FB_BOT_SIZE);
	} else {
		printk("Bottom fb region not available.\n");
		error = -ENOMEM;
		goto err_fbbotmem;
	}

	pdev = input_allocate_polled_device();
	if (!pdev) {
		printk(KERN_ERR "nintendo3ds_input.c: Not enough memory\n");
		error = -ENOMEM;
		goto err_alloc_pdev;
	}

	pdev->poll = nintendo3ds_input_poll;
	pdev->poll_interval = POLL_INTERVAL_DEFAULT;
	pdev->private = n3ds_input_dev;

	idev = pdev->input;
	idev->name = "Nintendo 3DS input";
	idev->phys = "nintendo3ds/input0";
	idev->id.bustype = BUS_HOST;
	idev->dev.parent = &plat_dev->dev;

	idev->evbit[0] |= BIT(EV_KEY);

	/* Direct button to key mapping
	for (i = 0; i < sizeof(button_map)/sizeof(*button_map); i++) {
		set_bit(button_map[i].code, idev->keybit);
	}*/
	for (i = 0; i < VKB_ROWS; i++) {
		for (j = 0; j < VKB_COLS; j++) {
			if (vkb_map_keys[i][j])
				set_bit(vkb_map_keys[i][j], idev->keybit);
		}
	}

	input_set_capability(idev, EV_MSC, MSC_SCAN);

	n3ds_input_dev->pdev = pdev;
	n3ds_input_dev->hid_input = hid_input;
	n3ds_input_dev->old_buttons = 0xFFF;
	n3ds_input_dev->fbbot = fbbot;
	n3ds_input_dev->font = get_default_font(SCREEN_BOT_W, SCREEN_BOT_H, -1, -1);
	vkb_init(&n3ds_input_dev->vkb);


	map_lcd_bot_fb();
	fbbot_clear_screen(fbbot, BLACK);
	fbbot_draw_vkb(fbbot, n3ds_input_dev->font, 0, 0, &n3ds_input_dev->vkb);
	//fbbot_draw_fillrect(fbbot, 50, 50, 70, 20, BLUE);
	//fbbot_draw_char(fbbot, n3ds_input_dev->font, 10, 10, GREEN, '@');
	//fbbot_draw_textf(fbbot, n3ds_input_dev->font, 10, 40, RED, "TROLOLOLO %d\nlel", n3ds_input_dev->old_buttons);

	error = input_register_polled_device(pdev);
	if (error) {
		printk(KERN_ERR "nintendo3ds_input.c: Failed to register device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	input_free_polled_device(pdev);
err_alloc_pdev:
	iounmap(fbbot);
	release_mem_region(FB_BOT_1, FB_BOT_SIZE);
err_fbbotmem:
	iounmap(hid_input);
	release_mem_region(HID_INPUT_PA, HID_INPUT_SIZE);
err_hidmem:
	kfree(n3ds_input_dev);
err_alloc_n3ds_input_dev:
	return error;
}

static int nintendo3ds_input_remove(struct platform_device *plat_pdev)
{
	struct nintendo3ds_input_dev *dev = platform_get_drvdata(plat_pdev);

	input_unregister_polled_device(dev->pdev);
	input_free_polled_device(dev->pdev);

	iounmap(dev->hid_input);
	release_mem_region(HID_INPUT_PA, HID_INPUT_SIZE);

	kfree(dev);

	return 0;
}

static const struct of_device_id nintendo3ds_input_of_match[] = {
	{ .compatible = "arm,nintendo3ds_input", },
	{},
};
MODULE_DEVICE_TABLE(of, nintendo3ds_input_of_match);

static struct platform_driver nintendo3ds_input_driver = {
	.probe	= nintendo3ds_input_probe,
	.remove	= nintendo3ds_input_remove,
	.driver	= {
		.name = "nintendo3ds_input",
		.owner = THIS_MODULE,
		.of_match_table = nintendo3ds_input_of_match,
	},
};

static int __init nintendo3ds_input_init_driver(void)
{
	return platform_driver_register(&nintendo3ds_input_driver);
}

static void __exit nintendo3ds_input_exit_driver(void)
{
	platform_driver_unregister(&nintendo3ds_input_driver);
}

module_init(nintendo3ds_input_init_driver);
module_exit(nintendo3ds_input_exit_driver);

MODULE_DESCRIPTION("Nintendo 3DS input driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nintendo3ds_input");
