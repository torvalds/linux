/*
 * nintendo3ds_codec_hid.c
 *
 * Copyright (C) 2016 Sergi Granell (xerpi)
 * Copyright (C) 2017 Paul LaMendola (paulguy)
 * based on ad7879-spi.c
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/jiffies.h>

#include <mach/bottom_lcd.h>
#include <mach/platform.h>

#define NINTENDO3DS_CODEC_HID_NAME	"nintendo3ds_codec_hid"
#define POLL_INTERVAL_DEFAULT		20
#define MAX_12BIT			((1 << 12) - 1)
#define CIRCLE_PAD_THRESHOLD		150
#define CIRCLE_PAD_FACTOR		150

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

#define BUTTON_HELD(b, m) (~(b) & (m))
#define BUTTON_PRESSED(b, o, m) ((~(b) & (o)) & (m))
#define BUTTON_CHANGED(b, o, m) (((b) ^ (o)) & (m))

#define VKB_ROWS (6)
#define VKB_COLS (17)

#define HIGHLIGHT_COLOR COLOR_RED

#define LEFT_SHIFTED  BIT(0)
#define RIGHT_SHIFTED BIT(1)

struct vkb_ctx_t {
	const struct font_desc *font;
	unsigned int key_locked[VKB_ROWS][VKB_COLS / sizeof(int) + 1];
	unsigned int x_offsets[VKB_ROWS][VKB_COLS];
	unsigned char x_sizes[VKB_ROWS][VKB_COLS];
	unsigned char last_key;
	bool locked_key;
	int held_row;
	int held_col;
	char shifted;
};

struct nintendo3ds_codec_hid {
	struct spi_device *spi;
	struct input_polled_dev *polled_dev;
	struct input_dev *input_dev;

	struct vkb_ctx_t vkb;
	unsigned long touch_jiffies;
	bool pendown;
};

/* VKB stuff */

/*
0123456789012345678901234567890123456789
Es 1 2 3 4 5 6 7 8 9 10 11 12 PSc SLk Bk
` 1 2 3 4 5 6 7 8 9 0 - = BSp Ins Hom PU
<> q w e r t y u i o p [ ]  \ Del End PD
Cap a s d f g h j k l ; ' Ent
LShf z x c v b n m , . / Rshf
Ctl M Alt Space Alt M Mnu Ctl
*/

static const char *vkb_map_normal[VKB_ROWS][VKB_COLS] = {
	{"Es", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "PSc", "SLk", "Bk"},
	{"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "BSp", "Ins", "Hom", "PU"},
	{"<>", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", " \\", "Del", "End", "PD"},
	{"Cap", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "Ent", NULL, NULL, NULL, NULL},
	{"LShf", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "RShf", NULL, NULL, NULL, NULL, NULL},
	{"Ctl", "M", "Alt", "Space", "Alt", "M", "Mnu", "Ctl", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

static const char *vkb_map_shift[VKB_ROWS][VKB_COLS] = {
	{"Es", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "PSc", "SLk", "Bk"},
	{"`", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+", "BSp", "Ins", "Hom", "PU"},
	{"<>", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{", "}", " |", "Del", "End", "PD"},
	{"Cap", "A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "\"", "Ent", NULL, NULL, NULL, NULL},
	{"LShf", "Z", "X", "C", "V", "B", "N", "M", "<", ">", "?", "RShf", NULL, NULL, NULL, NULL, NULL},
	{"Ctl", "M", "Alt", "Space", "Alt", "M", "Mnu", "Ctl", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

static const char vkb_map_keys[VKB_ROWS][VKB_COLS] = {
	{KEY_ESC, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_SYSRQ, KEY_SCROLLLOCK, KEY_PAUSE},
	{KEY_GRAVE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE, KEY_INSERT, KEY_HOME, KEY_PAGEUP},
	{KEY_TAB, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH, KEY_DELETE, KEY_END, KEY_PAGEDOWN},
	{KEY_CAPSLOCK, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON, KEY_COMMA, KEY_ENTER, 0, 0, 0, 0},
	{KEY_LEFTSHIFT, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_RIGHTSHIFT, 0, 0, 0, 0, 0},
	{KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFTALT, KEY_SPACE, KEY_RIGHTALT, KEY_RIGHTMETA, KEY_MENU, KEY_RIGHTCTRL, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

static void vkb_draw_key(const struct vkb_ctx_t *vkb, int row, int col) {
	unsigned int color;

	if(vkb->key_locked[row][col / sizeof(int)] & BIT(col % sizeof(int)))
		color = HIGHLIGHT_COLOR;
	else
		color = COLOR_WHITE;

	if(vkb->shifted) {
		if(vkb_map_shift[row][col]) {
			if (row == 0 || row == 5 || vkb_map_normal[row][col][1] != '\0')
				nintendo3ds_bottom_lcd_draw_text(vkb->font, vkb->x_offsets[row][col], row * vkb->font->height * 2, COLOR_BLACK, color,
						                               vkb_map_shift[row][col]);
			else
				nintendo3ds_bottom_lcd_draw_text(vkb->font, vkb->x_offsets[row][col], row * vkb->font->height * 2, color, COLOR_BLACK, 
						                               vkb_map_shift[row][col]);
		}
	} else {
		if(vkb_map_normal[row][col]) {
			if (row == 0 || row == 5 || vkb_map_normal[row][col][1] != '\0')
				nintendo3ds_bottom_lcd_draw_text(vkb->font, vkb->x_offsets[row][col], row * vkb->font->height * 2, COLOR_BLACK, color,
						                               vkb_map_normal[row][col]);
			else
				nintendo3ds_bottom_lcd_draw_text(vkb->font, vkb->x_offsets[row][col], row * vkb->font->height * 2, color, COLOR_BLACK, 
						                               vkb_map_normal[row][col]);
		}
	}
}

static void vkb_draw_bottom_lcd(const struct vkb_ctx_t *vkb)
{
	int i, j;

	for (j = 0; j < VKB_ROWS; j++) {
		for (i = 0; i < VKB_COLS; i++) {
			vkb_draw_key(vkb, j, i);
		}
	}
}

static int vkb_init(struct vkb_ctx_t *vkb)
{
	int x, i, j;

	vkb->font = get_default_font(NINTENDO3DS_LCD_BOT_WIDTH, NINTENDO3DS_LCD_BOT_HEIGHT, -1, -1);
	vkb->last_key = 0;
	vkb->locked_key = false;
	vkb->shifted = false;

	for(j = 0; j < VKB_ROWS; j++) {
		x = 0;
		for(i = 0; i < VKB_COLS; i++) {
			if (!vkb_map_normal[j][i]) {
				vkb->x_sizes[j][i] = 0;
				vkb->x_offsets[j][i] = 0;
				continue;
			}
			vkb->x_offsets[j][i] = x;
			vkb->x_sizes[j][i] = (strlen(vkb_map_normal[j][i]) + 1) * vkb->font->width;
			x += vkb->x_sizes[j][i];
		}
	}

	for(j = 0; j < VKB_ROWS; j++) {
		for(i = 0; i < VKB_COLS / sizeof(int) + 1; i++) {
			vkb->key_locked[j][i] = 0;
		}
	}

	nintendo3ds_bottom_lcd_clear_screen(COLOR_BLACK);
	vkb_draw_bottom_lcd(vkb);

	return 0;
}
/* End VKB stuff */

/* SPI stuff */
static int spi_write_2(struct spi_device *spi,
		       u8 *tx_buf0, u8 tx_len0,
		       u8 *tx_buf1, u8 tx_len1)
{
	struct spi_message msg;
	struct spi_transfer xfers[2];

	memset(xfers, 0, sizeof(xfers));

	xfers[0].tx_buf = tx_buf0;
	xfers[0].len = tx_len0;

	xfers[1].tx_buf = tx_buf1;
	xfers[1].len = tx_len1;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);

	return spi_sync(spi, &msg);
}

static int spi_write_read(struct spi_device *spi,
			  u8 *tx_buf, u8 tx_len,
			  u8 *rx_buf, u8 rx_len)
{
	struct spi_message msg;
	struct spi_transfer xfers[2];

	memset(xfers, 0, sizeof(xfers));

	xfers[0].tx_buf = tx_buf;
	xfers[0].len = tx_len;

	xfers[1].rx_buf = rx_buf;
	xfers[1].len = rx_len;

	spi_message_init(&msg);

	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);

	return spi_sync(spi, &msg);
}

static void spi_reg_select(struct spi_device *spi, u8 reg)
{
	u8 buffer1[4];
	u8 buffer2[0x40];

	buffer1[0] = 0;
	buffer2[0] = reg;

	spi_write_2(spi, buffer1, 1, buffer2, 1);
}

static u8 spi_reg_read_offset(struct spi_device *spi, u8 offset)
{
	u8 buffer_wr[8];
	u8 buffer_rd[0x40];

	buffer_wr[0] = 1 | (offset << 1);

	spi_write_read(spi, buffer_wr, 1, buffer_rd, 1);

	return buffer_rd[0];
}

static void spi_reg_write_offset(struct spi_device *spi, u8 reg, u8 val)
{
	u8 buffer1[8];
	u8 buffer2[0x40];

	buffer1[0] = (reg << 1); // Write
	buffer2[0] = val;

	spi_write_2(spi, buffer1, 1, buffer2, 1);
}

static void spi_reg_read_buffer(struct spi_device *spi,
			       u8 offset, void *buffer, u8 size)
{
	u8 buffer_wr[0x10];

	buffer_wr[0] = 1 | (offset << 1);

	spi_write_read(spi, buffer_wr, 1, buffer, size);
}

static void spi_reg_mask_offset(struct spi_device *spi, u8 offset, u8 mask0, u8 mask1)
{
	u8 buffer1[4];
	u8 buffer2[0x40];

	buffer1[0] = 1 | (offset << 1);

	spi_write_read(spi, buffer1, 1, buffer2, 1);

	buffer1[0] = offset << 1;
	buffer2[0] = (buffer2[0] & ~mask1) | (mask0 & mask1);

	spi_write_2(spi, buffer1, 1, buffer2, 1);
}

static void spi_codec_hid_initialize(struct spi_device *spi)
{
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x24, 0x98);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x26, 0x00);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x25, 0x43);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x24, 0x18);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x17, 0x43);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x19, 0x69);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x1B, 0x80);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x27, 0x11);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x26, 0xEC);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x24, 0x18);
	spi_reg_select(spi, 0x67);
	spi_reg_write_offset(spi, 0x25, 0x53);

	spi_reg_select(spi, 0x67);
	spi_reg_mask_offset(spi, 0x26, 0x80, 0x80);
	spi_reg_select(spi, 0x67);
	spi_reg_mask_offset(spi, 0x24, 0x00, 0x80);
	spi_reg_select(spi, 0x67);
	spi_reg_mask_offset(spi, 0x25, 0x10, 0x3C);
}

static void spi_codec_hid_request_data(struct spi_device *spi, u8 *buffer)
{
	spi_reg_select(spi, 0x67);
	spi_reg_read_offset(spi, 0x26);
	spi_reg_select(spi, 0xFB);
	spi_reg_read_buffer(spi, 1, buffer, 0x34);
}
/* End SPI stuff */

static void nintendo3ds_codec_hid_open(struct input_polled_dev *dev)
{
}

static void nintendo3ds_codec_hid_close(struct input_polled_dev *dev)
{
}

#define REPORT_KEY_BUTTON(key, button) \
	input_report_key(input_dev, key, BUTTON_HELD(buttons, button)); \

static void nintendo3ds_codec_hid_poll(struct input_polled_dev *polled_dev)
{
	struct nintendo3ds_codec_hid *codec_hid = polled_dev->private;
	struct input_dev *input_dev = codec_hid->input_dev;
	struct vkb_ctx_t *vkb = &codec_hid->vkb;

	u8 raw_data[0x40] __attribute__((aligned(sizeof(u16))));
	bool pendown;
	u16 raw_touch_x;
	u16 raw_touch_y;
	u16 screen_touch_x;
	u16 screen_touch_y;
	s16 raw_circlepad_x;
	s16 raw_circlepad_y;
	bool sync = false;
	int i, j;

	spi_codec_hid_request_data(codec_hid->spi, raw_data);

	raw_circlepad_x =
		(s16)le16_to_cpu(((raw_data[0x24] << 8) | raw_data[0x25]) & 0xFFF) - 2048;
	raw_circlepad_y =
		(s16)le16_to_cpu(((raw_data[0x14] << 8) | raw_data[0x15]) & 0xFFF) - 2048;

	if (abs(raw_circlepad_x) > CIRCLE_PAD_THRESHOLD) {
		input_report_rel(input_dev, REL_X,
				 -raw_circlepad_x / CIRCLE_PAD_FACTOR);
		sync = true;
	}

	if (abs(raw_circlepad_y) > CIRCLE_PAD_THRESHOLD) {
		input_report_rel(input_dev, REL_Y,
				 -raw_circlepad_y / CIRCLE_PAD_FACTOR);
		sync = true;
	}

	pendown = !(raw_data[0] & BIT(4));

	if (pendown) {
		if(!codec_hid->pendown) {
			raw_touch_x = le16_to_cpu((raw_data[0]  << 8) | raw_data[1]);
			raw_touch_y = le16_to_cpu((raw_data[10] << 8) | raw_data[11]);

			screen_touch_x = (u16)((u32)raw_touch_x * NINTENDO3DS_LCD_BOT_WIDTH / MAX_12BIT);
			screen_touch_y = (u16)((u32)raw_touch_y * NINTENDO3DS_LCD_BOT_HEIGHT / MAX_12BIT);

			for(j = 0; j < VKB_ROWS; j++) {
				for(i = 0; i < VKB_COLS; i++) {
					if(vkb->x_sizes[j][i] > 0 &&
					   screen_touch_x >= vkb->x_offsets[j][i] &&
					   screen_touch_x < vkb->x_offsets[j][i] + vkb->x_sizes[j][i] &&
					   screen_touch_y >= j * vkb->font->height * 2 &&
					   screen_touch_y < (j + 1) * vkb->font->height * 2) {
						codec_hid->pendown = true;

						codec_hid->touch_jiffies = jiffies;

						vkb->last_key = vkb_map_keys[j][i];
						if(vkb->key_locked[j][i / sizeof(int)] & BIT(i % sizeof(int))) {
							vkb->key_locked[j][i / sizeof(int)] &= ~BIT(i % sizeof(int));
							input_report_key(input_dev, vkb->last_key, 0);
							if(vkb->last_key == KEY_LEFTSHIFT)
								vkb->shifted &= ~LEFT_SHIFTED;
							else if(vkb->last_key == KEY_RIGHTSHIFT)
								vkb->shifted &= ~RIGHT_SHIFTED;

							if(vkb->shifted == 0)
								vkb_draw_bottom_lcd(vkb);

							vkb->locked_key = true;

							vkb_draw_key(vkb, j, i);
						} else {
							input_report_key(input_dev, vkb->last_key, 1);
						}

						vkb->held_row = j;
						vkb->held_col = i;

						sync = true;
						i = VKB_COLS;
						j = VKB_ROWS;
					}
				}
			}
		} else {
			if(!vkb->locked_key && time_is_before_jiffies(codec_hid->touch_jiffies + msecs_to_jiffies(500))) {
				vkb->key_locked[vkb->held_row][vkb->held_col / sizeof(int)] |= BIT(vkb->held_col % sizeof(int));
				vkb->locked_key = true;

				if(vkb_map_keys[vkb->held_row][vkb->held_col] == KEY_LEFTSHIFT)
					vkb->shifted |= LEFT_SHIFTED;
				else if(vkb_map_keys[vkb->held_row][vkb->held_col] == KEY_RIGHTSHIFT)
					vkb->shifted |= RIGHT_SHIFTED;

				if(vkb->shifted != 0)
					vkb_draw_bottom_lcd(vkb);

				vkb_draw_key(vkb, vkb->held_row, vkb->held_col);
			}
		}
	} else {
		codec_hid->pendown = false;

		if(vkb->locked_key) {
			vkb->locked_key = false;
		} else {
			if(vkb->last_key) {
				input_report_key(input_dev, vkb->last_key, 0);
				sync = true;
			}
		}

		vkb->last_key = 0;
	}

	if(sync)
		input_sync(input_dev);
}

static int nintendo3ds_codec_hid_probe(struct spi_device *spi)
{
	struct nintendo3ds_codec_hid *codec_hid;
	struct input_polled_dev *polled_dev;
	struct input_dev *input_dev;
	int err;
	int i, j;

	/* SPI circle pad and touchscreen stuff */
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;

	err = spi_setup(spi);
	if (err < 0) {
		dev_err(&spi->dev, "%s: SPI setup error %d\n",
			__func__, err);
		goto err_spi_setup;
	}

	codec_hid = devm_kzalloc(&spi->dev, sizeof(struct nintendo3ds_codec_hid), GFP_KERNEL);
	if (!codec_hid) {
		err = -ENOMEM;
		goto err_alloc_codec_hid;
	}


	polled_dev = devm_input_allocate_polled_device(&spi->dev);
	if (!polled_dev) {
		dev_err(&spi->dev, "%s: Can't allocate input device, error %d\n",
			__func__, err);
		err = -ENOMEM;
		goto err_alloc_polled_dev;
	}

	polled_dev->private = codec_hid;
	polled_dev->poll = nintendo3ds_codec_hid_poll;
	polled_dev->open = nintendo3ds_codec_hid_open;
	polled_dev->close = nintendo3ds_codec_hid_close;
	polled_dev->poll_interval = POLL_INTERVAL_DEFAULT;

	input_dev = polled_dev->input;
	input_dev->name = "Nintendo 3DS CODEC HID";
	input_dev->phys = NINTENDO3DS_CODEC_HID_NAME "/input0";
	input_dev->id.bustype = BUS_SPI;
	input_dev->dev.parent = &spi->dev;

	/* circle pad/mouse stuff */
	set_bit(EV_REL, input_dev->evbit);
	set_bit(REL_X, input_dev->relbit);
	set_bit(REL_Y, input_dev->relbit);
	set_bit(REL_WHEEL, input_dev->relbit);

	/* Enable VKB keys */
	set_bit(EV_KEY, input_dev->evbit);

	for (i = 0; i < VKB_ROWS; i++) {
		for (j = 0; j < VKB_COLS; j++) {
			if (vkb_map_keys[i][j])
				set_bit(vkb_map_keys[i][j], input_dev->keybit);
		}
	}

	/* dunno what this does but the other driver sets this */
	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	codec_hid->spi = spi;
	codec_hid->polled_dev = polled_dev;
	codec_hid->input_dev = polled_dev->input;
	vkb_init(&codec_hid->vkb);
	spi_set_drvdata(spi, codec_hid);

	spi_codec_hid_initialize(spi);

	err = input_register_polled_device(polled_dev);
	if (err) {
		pr_err("nintendo3ds_codec_hid.c: Failed to register input device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	input_unregister_polled_device(polled_dev);
	input_free_polled_device(polled_dev);
	devm_kfree(&spi->dev, codec_hid);
err_alloc_polled_dev:
	kfree(codec_hid);
err_alloc_codec_hid:
err_spi_setup:
	return err;
}

#ifdef CONFIG_OF
static const struct of_device_id nintendo3ds_codec_hid_dt_ids[] = {
	{ .compatible = "nintendo3ds,codec-hid", },
	{ }
};
MODULE_DEVICE_TABLE(of, nintendo3ds_codec_hid_dt_ids);
#endif

static struct spi_driver nintendo3ds_codec_hid_driver = {
	.driver = {
		.name	= NINTENDO3DS_CODEC_HID_NAME,
		.of_match_table = of_match_ptr(nintendo3ds_codec_hid_dt_ids),
	},
	.probe		= nintendo3ds_codec_hid_probe
};

module_spi_driver(nintendo3ds_codec_hid_driver);

MODULE_AUTHOR("Sergi Granell <xerpi.g.12@gmail.com>");
MODULE_DESCRIPTION("Nintendo 3DS CODEC HID driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:" NINTENDO3DS_CODEC_HID_NAME);
