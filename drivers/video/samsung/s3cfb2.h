/* linux/drivers/video/samsung/s3cfb2.h
 *
 * Header file for Samsung Display Driver (FIMD) driver
 *
 * Jinsung Yang, Copyright (c) 2009 Samsung Electronics
 * 	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _S3CFB_H
#define _S3CFB_H

#ifdef __KERNEL__
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include <plat/fb.h>
#endif

/*
 * C O M M O N   D E F I N I T I O N S
 *
*/
#define S3CFB_NAME		"s3cfb"

#define info(args...)		do { printk(KERN_INFO S3CFB_NAME ": " args); } while (0)
#define err(args...)		do { printk(KERN_ERR  S3CFB_NAME ": " args); } while (0)

#define S3CFB_AVALUE(r, g, b)	(((r & 0xf) << 8) | ((g & 0xf) << 4) | ((b & 0xf) << 0))
#define S3CFB_CHROMA(r, g, b)	(((r & 0xff) << 16) | ((g & 0xff) << 8) | ((b & 0xff) << 0))


/*
 * E N U M E R A T I O N S
 *
*/
enum s3cfb_data_path_t {
	DATA_PATH_FIFO = 0,
	DATA_PATH_DMA = 1,
};

enum s3cfb_alpha_t {
	PLANE_BLENDING,
	PIXEL_BLENDING,
};

enum s3cfb_chroma_dir_t {
	CHROMA_FG,
	CHROMA_BG,
};

enum s3cfb_output_t {
	OUTPUT_RGB,
	OUTPUT_ITU,
	OUTPUT_I80LDI0,
	OUTPUT_I80LDI1,
	OUTPUT_TV,
	OUTPUT_TV_RGB,
	OUTPUT_TV_I80LDI0,
	OUTPUT_TV_I80LDI1,
};

enum s3cfb_rgb_mode_t {
	MODE_RGB_P = 0,
	MODE_BGR_P = 1,
	MODE_RGB_S = 2,
	MODE_BGR_S = 3,
};


/*
 * F I M D   S T R U C T U R E S
 *
*/

/*
 * struct s3cfb_alpha
 * @mode:		blending method (plane/pixel)
 * @channel:		alpha channel (0/1)
 * @value:		alpha value (for plane blending)
*/
struct s3cfb_alpha {
	enum		s3cfb_alpha_t mode;
	int		channel;
	unsigned int	value;
};

/*
 * struct s3cfb_chroma
 * @enabled:		if chroma key function enabled
 * @blended:		if chroma key alpha blending enabled (unused)
 * @key:		chroma value to be applied
 * @comp_key:		compare key (unused)
 * @alpha:		alpha value for chroma (unused)
 * @dir:		chroma key direction (fg/bg, fixed to fg)
 *
*/
struct s3cfb_chroma {
	int		enabled;
	int		blended;
	unsigned int	key;
	unsigned int	comp_key;
	unsigned int	alpha;
	enum		s3cfb_chroma_dir_t dir;
};

/*
 * struct s3cfb_lcd_polarity
 * @rise_vclk:	if 1, video data is fetched at rising edge
 * @inv_hsync:	if HSYNC polarity is inversed
 * @inv_vsync:	if VSYNC polarity is inversed
 * @inv_vden:	if VDEN polarity is inversed
*/
struct s3cfb_lcd_polarity {
	int	rise_vclk;
	int	inv_hsync;
	int	inv_vsync;
	int	inv_vden;
};

/*
 * struct s3cfb_lcd_timing
 * @h_fp:	horizontal front porch
 * @h_bp:	horizontal back porch
 * @h_sw:	horizontal sync width
 * @v_fp:	vertical front porch
 * @v_fpe:	vertical front porch for even field
 * @v_bp:	vertical back porch
 * @v_bpe:	vertical back porch for even field
*/
struct s3cfb_lcd_timing {
	int	h_fp;
	int	h_bp;
	int	h_sw;
	int	v_fp;
	int	v_fpe;
	int	v_bp;
	int	v_bpe;
	int	v_sw;
};

/*
 * struct s3cfb_lcd
 * @width:		horizontal resolution
 * @height:		vertical resolution
 * @bpp:		bits per pixel
 * @freq:		vframe frequency
 * @timing:		timing values
 * @polarity:		polarity settings
 * @init_ldi:		pointer to LDI init function
 *
*/
struct s3cfb_lcd {
	int	width;
	int	height;
	int	bpp;
	int	freq;
	struct	s3cfb_lcd_timing timing;
	struct	s3cfb_lcd_polarity polarity;

	void	(*init_ldi)(void);
};

/*
 * struct s3cfb_window
 * @id:			window id
 * @enabled:		if enabled
 * @x:			left x of start offset
 * @y:			top y of start offset
 * @path:		data path (dma/fifo)
 * @local_channel:	local channel for fifo path (0/1)
 * @dma_burst:		dma burst length (4/8/16)
 * @unpacked:		if unpacked format is
 * @pseudo_pal:		pseudo palette for fb layer
 * @alpha:		alpha blending structure
 * @chroma:		chroma key structure
*/
struct s3cfb_window {
	int			id;
	int			enabled;
	atomic_t		in_use;
	int			x;
	int			y;
	enum			s3cfb_data_path_t path;
	int			local_channel;
	int			dma_burst;
	unsigned int		pseudo_pal[16];
	struct			s3cfb_alpha alpha;
	struct			s3cfb_chroma chroma;
	int			(*suspend_fifo)(void);
	int			(*resume_fifo)(void);
};

/*
 * struct s3cfb_global
 *
 * @fb:			pointer to fb_info
 * @enabled:		if signal output enabled
 * @dsi:		if mipi-dsim enabled
 * @interlace:		if interlace format is used
 * @output:		output path (RGB/I80/Etc)
 * @rgb_mode:		RGB mode
 * @lcd:		pointer to lcd structure
*/
struct s3cfb_global {
	/* general */
	void __iomem		*regs;
	struct mutex		lock;
	struct device		*dev;
	struct clk		*clock;
	int			irq;
	wait_queue_head_t	wq;
	unsigned int		wq_count;
	struct fb_info		**fb;

	/* fimd */
	int			enabled;
	int			dsi;
	int			interlace;
	enum s3cfb_output_t	output;
	enum s3cfb_rgb_mode_t	rgb_mode;
	struct s3cfb_lcd	*lcd;
};


/*
 * S T R U C T U R E S  F O R  C U S T O M  I O C T L S
 *
*/
struct s3cfb_user_window {
	int x;
	int y;
};

struct s3cfb_user_plane_alpha {
	int		channel;
	unsigned char	red;
	unsigned char	green;
	unsigned char	blue;
};

struct s3cfb_user_chroma {
	int		enabled;
	unsigned char	red;
	unsigned char	green;
	unsigned char	blue;
};


/*
 * C U S T O M  I O C T L S
 *
*/
#define S3CFB_WIN_ON			_IO  ('F', 200)
#define S3CFB_WIN_OFF			_IO  ('F', 201)
#define S3CFB_WIN_OFF_ALL		_IO  ('F', 202)
#define S3CFB_WIN_POSITION		_IOW ('F', 203, struct s3cfb_user_window)
#define S3CFB_WIN_SET_PLANE_ALPHA	_IOW ('F', 204, struct s3cfb_user_plane_alpha)
#define S3CFB_WIN_SET_CHROMA		_IOW ('F', 205, struct s3cfb_user_chroma)
#define S3CFB_SET_VSYNC_INT		_IOW ('F', 206, unsigned int)
#define S3CFB_SET_SUSPEND_FIFO		_IOW ('F', 300, unsigned long)
#define S3CFB_SET_RESUME_FIFO		_IOW ('F', 301, unsigned long)


/*
 * E X T E R N S
 *
*/
extern int soft_cursor(struct fb_info *info, struct fb_cursor *cursor);
extern void s3cfb_set_lcd_info(struct s3cfb_global *ctrl);
extern struct s3c_platform_fb *to_fb_plat(struct device *dev);
extern void s3cfb_check_line_count(struct s3cfb_global *ctrl);
extern int s3cfb_set_output(struct s3cfb_global *ctrl);
extern int s3cfb_set_display_mode(struct s3cfb_global *ctrl);
extern int s3cfb_display_on(struct s3cfb_global *ctrl);
extern int s3cfb_display_off(struct s3cfb_global *ctrl);
extern int s3cfb_frame_off(struct s3cfb_global *ctrl);
extern int s3cfb_set_clock(struct s3cfb_global *ctrl);
extern int s3cfb_set_polarity(struct s3cfb_global *ctrl);
extern int s3cfb_set_timing(struct s3cfb_global *ctrl);
extern int s3cfb_set_lcd_size(struct s3cfb_global *ctrl);
extern int s3cfb_set_global_interrupt(struct s3cfb_global *ctrl, int enable);
extern int s3cfb_set_vsync_interrupt(struct s3cfb_global *ctrl, int enable);
extern int s3cfb_set_fifo_interrupt(struct s3cfb_global *ctrl, int enable);
extern int s3cfb_clear_interrupt(struct s3cfb_global *ctrl);
extern int s3cfb_window_on(struct s3cfb_global *ctrl, int id);
extern int s3cfb_window_off(struct s3cfb_global *ctrl, int id);
extern int s3cfb_set_window_control(struct s3cfb_global *ctrl, int id);
extern int s3cfb_set_alpha_blending(struct s3cfb_global *ctrl, int id);
extern int s3cfb_set_window_position(struct s3cfb_global *ctrl, int id);
extern int s3cfb_set_window_size(struct s3cfb_global *ctrl, int id);
extern int s3cfb_set_buffer_address(struct s3cfb_global *ctrl, int id);
extern int s3cfb_set_buffer_size(struct s3cfb_global *ctrl, int id);
extern int s3cfb_set_chroma_key(struct s3cfb_global *ctrl, int id);

#endif /* _S3CFB_H */
