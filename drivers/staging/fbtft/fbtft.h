/*
 * Copyright (C) 2013 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_FBTFT_H
#define __LINUX_FBTFT_H

#include <linux/fb.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>


#define FBTFT_NOP		0x00
#define FBTFT_SWRESET	0x01
#define FBTFT_RDDID		0x04
#define FBTFT_RDDST		0x09
#define FBTFT_CASET		0x2A
#define FBTFT_RASET		0x2B
#define FBTFT_RAMWR		0x2C

#define FBTFT_ONBOARD_BACKLIGHT 2

#define FBTFT_GPIO_NO_MATCH		0xFFFF
#define FBTFT_GPIO_NAME_SIZE	32
#define FBTFT_MAX_INIT_SEQUENCE      512
#define FBTFT_GAMMA_MAX_VALUES_TOTAL 128

#define FBTFT_OF_INIT_CMD	BIT(24)
#define FBTFT_OF_INIT_DELAY	BIT(25)

/**
 * struct fbtft_gpio - Structure that holds one pinname to gpio mapping
 * @name: pinname (reset, dc, etc.)
 * @gpio: GPIO number
 *
 */
struct fbtft_gpio {
	char name[FBTFT_GPIO_NAME_SIZE];
	unsigned gpio;
};

struct fbtft_par;

/**
 * struct fbtft_ops - FBTFT operations structure
 * @write: Writes to interface bus
 * @read: Reads from interface bus
 * @write_vmem: Writes video memory to display
 * @write_reg: Writes to controller register
 * @set_addr_win: Set the GRAM update window
 * @reset: Reset the LCD controller
 * @mkdirty: Marks display lines for update
 * @update_display: Updates the display
 * @init_display: Initializes the display
 * @blank: Blank the display (optional)
 * @request_gpios_match: Do pinname to gpio matching
 * @request_gpios: Request gpios from the kernel
 * @free_gpios: Free previously requested gpios
 * @verify_gpios: Verify that necessary gpios is present (optional)
 * @register_backlight: Used to register backlight device (optional)
 * @unregister_backlight: Unregister backlight device (optional)
 * @set_var: Configure LCD with values from variables like @rotate and @bgr
 *           (optional)
 * @set_gamma: Set Gamma curve (optional)
 *
 * Most of these operations have default functions assigned to them in
 *     fbtft_framebuffer_alloc()
 */
struct fbtft_ops {
	int (*write)(struct fbtft_par *par, void *buf, size_t len);
	int (*read)(struct fbtft_par *par, void *buf, size_t len);
	int (*write_vmem)(struct fbtft_par *par, size_t offset, size_t len);
	void (*write_register)(struct fbtft_par *par, int len, ...);

	void (*set_addr_win)(struct fbtft_par *par,
		int xs, int ys, int xe, int ye);
	void (*reset)(struct fbtft_par *par);
	void (*mkdirty)(struct fb_info *info, int from, int to);
	void (*update_display)(struct fbtft_par *par,
				unsigned start_line, unsigned end_line);
	int (*init_display)(struct fbtft_par *par);
	int (*blank)(struct fbtft_par *par, bool on);

	unsigned long (*request_gpios_match)(struct fbtft_par *par,
		const struct fbtft_gpio *gpio);
	int (*request_gpios)(struct fbtft_par *par);
	int (*verify_gpios)(struct fbtft_par *par);

	void (*register_backlight)(struct fbtft_par *par);
	void (*unregister_backlight)(struct fbtft_par *par);

	int (*set_var)(struct fbtft_par *par);
	int (*set_gamma)(struct fbtft_par *par, unsigned long *curves);
};

/**
 * struct fbtft_display - Describes the display properties
 * @width: Width of display in pixels
 * @height: Height of display in pixels
 * @regwidth: LCD Controller Register width in bits
 * @buswidth: Display interface bus width in bits
 * @backlight: Backlight type.
 * @fbtftops: FBTFT operations provided by driver or device (platform_data)
 * @bpp: Bits per pixel
 * @fps: Frames per second
 * @txbuflen: Size of transmit buffer
 * @init_sequence: Pointer to LCD initialization array
 * @gamma: String representation of Gamma curve(s)
 * @gamma_num: Number of Gamma curves
 * @gamma_len: Number of values per Gamma curve
 * @debug: Initial debug value
 *
 * This structure is not stored by FBTFT except for init_sequence.
 */
struct fbtft_display {
	unsigned width;
	unsigned height;
	unsigned regwidth;
	unsigned buswidth;
	unsigned backlight;
	struct fbtft_ops fbtftops;
	unsigned bpp;
	unsigned fps;
	int txbuflen;
	int *init_sequence;
	char *gamma;
	int gamma_num;
	int gamma_len;
	unsigned long debug;
};

/**
 * struct fbtft_platform_data - Passes display specific data to the driver
 * @display: Display properties
 * @gpios: Pointer to an array of pinname to gpio mappings
 * @rotate: Display rotation angle
 * @bgr: LCD Controller BGR bit
 * @fps: Frames per second (this will go away, use @fps in @fbtft_display)
 * @txbuflen: Size of transmit buffer
 * @startbyte: When set, enables use of Startbyte in transfers
 * @gamma: String representation of Gamma curve(s)
 * @extra: A way to pass extra info
 */
struct fbtft_platform_data {
	struct fbtft_display display;
	const struct fbtft_gpio *gpios;
	unsigned rotate;
	bool bgr;
	unsigned fps;
	int txbuflen;
	u8 startbyte;
	char *gamma;
	void *extra;
};

/**
 * struct fbtft_par - Main FBTFT data structure
 *
 * This structure holds all relevant data to operate the display
 *
 * See sourcefile for documentation since nested structs is not
 * supported by kernel-doc.
 *
 */
/* @spi: Set if it is a SPI device
 * @pdev: Set if it is a platform device
 * @info: Pointer to framebuffer fb_info structure
 * @pdata: Pointer to platform data
 * @ssbuf: Not used
 * @pseudo_palette: Used by fb_set_colreg()
 * @txbuf.buf: Transmit buffer
 * @txbuf.len: Transmit buffer length
 * @buf: Small buffer used when writing init data over SPI
 * @startbyte: Used by some controllers when in SPI mode.
 *             Format: 6 bit Device id + RS bit + RW bit
 * @fbtftops: FBTFT operations provided by driver or device (platform_data)
 * @dirty_lock: Protects dirty_lines_start and dirty_lines_end
 * @dirty_lines_start: Where to begin updating display
 * @dirty_lines_end: Where to end updating display
 * @gpio.reset: GPIO used to reset display
 * @gpio.dc: Data/Command signal, also known as RS
 * @gpio.rd: Read latching signal
 * @gpio.wr: Write latching signal
 * @gpio.latch: Bus latch signal, eg. 16->8 bit bus latch
 * @gpio.cs: LCD Chip Select with parallel interface bus
 * @gpio.db[16]: Parallel databus
 * @gpio.led[16]: Led control signals
 * @gpio.aux[16]: Auxiliary signals, not used by core
 * @init_sequence: Pointer to LCD initialization array
 * @gamma.lock: Mutex for Gamma curve locking
 * @gamma.curves: Pointer to Gamma curve array
 * @gamma.num_values: Number of values per Gamma curve
 * @gamma.num_curves: Number of Gamma curves
 * @debug: Pointer to debug value
 * @current_debug:
 * @first_update_done: Used to only time the first display update
 * @update_time: Used to calculate 'fps' in debug output
 * @bgr: BGR mode/\n
 * @extra: Extra info needed by driver
 */
struct fbtft_par {
	struct spi_device *spi;
	struct platform_device *pdev;
	struct fb_info *info;
	struct fbtft_platform_data *pdata;
	u16 *ssbuf;
	u32 pseudo_palette[16];
	struct {
		void *buf;
		dma_addr_t dma;
		size_t len;
	} txbuf;
	u8 *buf;
	u8 startbyte;
	struct fbtft_ops fbtftops;
	spinlock_t dirty_lock;
	unsigned dirty_lines_start;
	unsigned dirty_lines_end;
	struct {
		int reset;
		int dc;
		int rd;
		int wr;
		int latch;
		int cs;
		int db[16];
		int led[16];
		int aux[16];
	} gpio;
	int *init_sequence;
	struct {
		struct mutex lock;
		unsigned long *curves;
		int num_values;
		int num_curves;
	} gamma;
	unsigned long debug;
	bool first_update_done;
	struct timespec update_time;
	bool bgr;
	void *extra;
};

#define NUMARGS(...)  (sizeof((int[]){__VA_ARGS__})/sizeof(int))

#define write_reg(par, ...)                                              \
	par->fbtftops.write_register(par, NUMARGS(__VA_ARGS__), __VA_ARGS__)

/* fbtft-core.c */
void fbtft_dbg_hex(const struct device *dev, int groupsize,
		   void *buf, size_t len, const char *fmt, ...);
struct fb_info *fbtft_framebuffer_alloc(struct fbtft_display *display,
					struct device *dev,
					struct fbtft_platform_data *pdata);
void fbtft_framebuffer_release(struct fb_info *info);
int fbtft_register_framebuffer(struct fb_info *fb_info);
int fbtft_unregister_framebuffer(struct fb_info *fb_info);
void fbtft_register_backlight(struct fbtft_par *par);
void fbtft_unregister_backlight(struct fbtft_par *par);
int fbtft_init_display(struct fbtft_par *par);
int fbtft_probe_common(struct fbtft_display *display, struct spi_device *sdev,
		       struct platform_device *pdev);
int fbtft_remove_common(struct device *dev, struct fb_info *info);

/* fbtft-io.c */
int fbtft_write_spi(struct fbtft_par *par, void *buf, size_t len);
int fbtft_write_spi_emulate_9(struct fbtft_par *par, void *buf, size_t len);
int fbtft_read_spi(struct fbtft_par *par, void *buf, size_t len);
int fbtft_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len);
int fbtft_write_gpio16_wr(struct fbtft_par *par, void *buf, size_t len);
int fbtft_write_gpio16_wr_latched(struct fbtft_par *par, void *buf, size_t len);

/* fbtft-bus.c */
int fbtft_write_vmem8_bus8(struct fbtft_par *par, size_t offset, size_t len);
int fbtft_write_vmem16_bus16(struct fbtft_par *par, size_t offset, size_t len);
int fbtft_write_vmem16_bus8(struct fbtft_par *par, size_t offset, size_t len);
int fbtft_write_vmem16_bus9(struct fbtft_par *par, size_t offset, size_t len);
void fbtft_write_reg8_bus8(struct fbtft_par *par, int len, ...);
void fbtft_write_reg8_bus9(struct fbtft_par *par, int len, ...);
void fbtft_write_reg16_bus8(struct fbtft_par *par, int len, ...);
void fbtft_write_reg16_bus16(struct fbtft_par *par, int len, ...);


#define FBTFT_REGISTER_DRIVER(_name, _compatible, _display)                \
									   \
static int fbtft_driver_probe_spi(struct spi_device *spi)                  \
{                                                                          \
	return fbtft_probe_common(_display, spi, NULL);                    \
}                                                                          \
									   \
static int fbtft_driver_remove_spi(struct spi_device *spi)                 \
{                                                                          \
	struct fb_info *info = spi_get_drvdata(spi);                       \
									   \
	return fbtft_remove_common(&spi->dev, info);                       \
}                                                                          \
									   \
static int fbtft_driver_probe_pdev(struct platform_device *pdev)           \
{                                                                          \
	return fbtft_probe_common(_display, NULL, pdev);                   \
}                                                                          \
									   \
static int fbtft_driver_remove_pdev(struct platform_device *pdev)          \
{                                                                          \
	struct fb_info *info = platform_get_drvdata(pdev);                 \
									   \
	return fbtft_remove_common(&pdev->dev, info);                      \
}                                                                          \
									   \
static const struct of_device_id dt_ids[] = {                              \
	{ .compatible = _compatible },                                     \
	{},                                                                \
};                                                                         \
									   \
MODULE_DEVICE_TABLE(of, dt_ids);                                           \
									   \
									   \
static struct spi_driver fbtft_driver_spi_driver = {                       \
	.driver = {                                                        \
		.name   = _name,                                           \
		.owner  = THIS_MODULE,                                     \
		.of_match_table = of_match_ptr(dt_ids),                    \
	},                                                                 \
	.probe  = fbtft_driver_probe_spi,                                  \
	.remove = fbtft_driver_remove_spi,                                 \
};                                                                         \
									   \
static struct platform_driver fbtft_driver_platform_driver = {             \
	.driver = {                                                        \
		.name   = _name,                                           \
		.owner  = THIS_MODULE,                                     \
		.of_match_table = of_match_ptr(dt_ids),                    \
	},                                                                 \
	.probe  = fbtft_driver_probe_pdev,                                 \
	.remove = fbtft_driver_remove_pdev,                                \
};                                                                         \
									   \
static int __init fbtft_driver_module_init(void)                           \
{                                                                          \
	int ret;                                                           \
									   \
	ret = spi_register_driver(&fbtft_driver_spi_driver);               \
	if (ret < 0)                                                       \
		return ret;                                                \
	return platform_driver_register(&fbtft_driver_platform_driver);    \
}                                                                          \
									   \
static void __exit fbtft_driver_module_exit(void)                          \
{                                                                          \
	spi_unregister_driver(&fbtft_driver_spi_driver);                   \
	platform_driver_unregister(&fbtft_driver_platform_driver);         \
}                                                                          \
									   \
module_init(fbtft_driver_module_init);                                     \
module_exit(fbtft_driver_module_exit);


/* Debug macros */

/* shorthand debug levels */
#define DEBUG_LEVEL_1	DEBUG_REQUEST_GPIOS
#define DEBUG_LEVEL_2	(DEBUG_LEVEL_1 | DEBUG_DRIVER_INIT_FUNCTIONS | DEBUG_TIME_FIRST_UPDATE)
#define DEBUG_LEVEL_3	(DEBUG_LEVEL_2 | DEBUG_RESET | DEBUG_INIT_DISPLAY | DEBUG_BLANK | DEBUG_REQUEST_GPIOS | DEBUG_FREE_GPIOS | DEBUG_VERIFY_GPIOS | DEBUG_BACKLIGHT | DEBUG_SYSFS)
#define DEBUG_LEVEL_4	(DEBUG_LEVEL_2 | DEBUG_FB_READ | DEBUG_FB_WRITE | DEBUG_FB_FILLRECT | DEBUG_FB_COPYAREA | DEBUG_FB_IMAGEBLIT | DEBUG_FB_BLANK)
#define DEBUG_LEVEL_5	(DEBUG_LEVEL_3 | DEBUG_UPDATE_DISPLAY)
#define DEBUG_LEVEL_6	(DEBUG_LEVEL_4 | DEBUG_LEVEL_5)
#define DEBUG_LEVEL_7	0xFFFFFFFF

#define DEBUG_DRIVER_INIT_FUNCTIONS (1<<3)
#define DEBUG_TIME_FIRST_UPDATE     (1<<4)
#define DEBUG_TIME_EACH_UPDATE      (1<<5)
#define DEBUG_DEFERRED_IO           (1<<6)
#define DEBUG_FBTFT_INIT_FUNCTIONS  (1<<7)

/* fbops */
#define DEBUG_FB_READ               (1<<8)
#define DEBUG_FB_WRITE              (1<<9)
#define DEBUG_FB_FILLRECT           (1<<10)
#define DEBUG_FB_COPYAREA           (1<<11)
#define DEBUG_FB_IMAGEBLIT          (1<<12)
#define DEBUG_FB_SETCOLREG          (1<<13)
#define DEBUG_FB_BLANK              (1<<14)

#define DEBUG_SYSFS                 (1<<16)

/* fbtftops */
#define DEBUG_BACKLIGHT             (1<<17)
#define DEBUG_READ                  (1<<18)
#define DEBUG_WRITE                 (1<<19)
#define DEBUG_WRITE_VMEM            (1<<20)
#define DEBUG_WRITE_REGISTER        (1<<21)
#define DEBUG_SET_ADDR_WIN          (1<<22)
#define DEBUG_RESET                 (1<<23)
#define DEBUG_MKDIRTY               (1<<24)
#define DEBUG_UPDATE_DISPLAY        (1<<25)
#define DEBUG_INIT_DISPLAY          (1<<26)
#define DEBUG_BLANK                 (1<<27)
#define DEBUG_REQUEST_GPIOS         (1<<28)
#define DEBUG_FREE_GPIOS            (1<<29)
#define DEBUG_REQUEST_GPIOS_MATCH   (1<<30)
#define DEBUG_VERIFY_GPIOS          (1<<31)


#define fbtft_init_dbg(dev, format, arg...)                  \
do {                                                         \
	if (unlikely((dev)->platform_data &&                 \
	    (((struct fbtft_platform_data *)(dev)->platform_data)->display.debug & DEBUG_DRIVER_INIT_FUNCTIONS))) \
		dev_info(dev, format, ##arg);                \
} while (0)

#define fbtft_par_dbg(level, par, format, arg...)            \
do {                                                         \
	if (unlikely(par->debug & level))                    \
		dev_info(par->info->device, format, ##arg);  \
} while (0)


#define fbtft_par_dbg_hex(level, par, dev, type, buf, num, format, arg...) \
do {                                                                       \
	if (unlikely(par->debug & level))                                  \
		fbtft_dbg_hex(dev, sizeof(type), buf, num * sizeof(type), format, ##arg); \
} while (0)

#endif /* __LINUX_FBTFT_H */
