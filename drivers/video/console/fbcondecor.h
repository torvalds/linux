/* 
 *  linux/drivers/video/console/fbcondecor.h -- Framebuffer Console Decoration headers
 *
 *  Copyright (C) 2004 Michal Januszewski <spock@gentoo.org>
 *
 */

#ifndef __FBCON_DECOR_H
#define __FBCON_DECOR_H

#ifndef _LINUX_FB_H
#include <linux/fb.h>
#endif

/* This is needed for vc_cons in fbcmap.c */
#include <linux/vt_kern.h>

struct fb_cursor;
struct fb_info;
struct vc_data;

#ifdef CONFIG_FB_CON_DECOR
/* fbcondecor.c */
int fbcon_decor_init(void);
int fbcon_decor_exit(void);
int fbcon_decor_call_helper(char* cmd, unsigned short cons);
int fbcon_decor_disable(struct vc_data *vc, unsigned char redraw);

/* cfbcondecor.c */
void fbcon_decor_putcs(struct vc_data *vc, struct fb_info *info, const unsigned short *s, int count, int yy, int xx);
void fbcon_decor_cursor(struct fb_info *info, struct fb_cursor *cursor);
void fbcon_decor_clear(struct vc_data *vc, struct fb_info *info, int sy, int sx, int height, int width);
void fbcon_decor_clear_margins(struct vc_data *vc, struct fb_info *info, int bottom_only);
void fbcon_decor_blank(struct vc_data *vc, struct fb_info *info, int blank);
void fbcon_decor_bmove_redraw(struct vc_data *vc, struct fb_info *info, int y, int sx, int dx, int width);
void fbcon_decor_copy(u8 *dst, u8 *src, int height, int width, int linebytes, int srclinesbytes, int bpp);
void fbcon_decor_fix_pseudo_pal(struct fb_info *info, struct vc_data *vc);

/* vt.c */
void acquire_console_sem(void);
void release_console_sem(void);
void do_unblank_screen(int entering_gfx);

/* struct vc_data *y */
#define fbcon_decor_active_vc(y) (y->vc_decor.state && y->vc_decor.theme) 

/* struct fb_info *x, struct vc_data *y */
#define fbcon_decor_active_nores(x,y) (x->bgdecor.data && fbcon_decor_active_vc(y))

/* struct fb_info *x, struct vc_data *y */
#define fbcon_decor_active(x,y) (fbcon_decor_active_nores(x,y) &&		\
			      x->bgdecor.width == x->var.xres && 	\
			      x->bgdecor.height == x->var.yres &&	\
			      x->bgdecor.depth == x->var.bits_per_pixel)


#else /* CONFIG_FB_CON_DECOR */

static inline void fbcon_decor_putcs(struct vc_data *vc, struct fb_info *info, const unsigned short *s, int count, int yy, int xx) {}
static inline void fbcon_decor_putc(struct vc_data *vc, struct fb_info *info, int c, int ypos, int xpos) {}
static inline void fbcon_decor_cursor(struct fb_info *info, struct fb_cursor *cursor) {}
static inline void fbcon_decor_clear(struct vc_data *vc, struct fb_info *info, int sy, int sx, int height, int width) {}
static inline void fbcon_decor_clear_margins(struct vc_data *vc, struct fb_info *info, int bottom_only) {}
static inline void fbcon_decor_blank(struct vc_data *vc, struct fb_info *info, int blank) {}
static inline void fbcon_decor_bmove_redraw(struct vc_data *vc, struct fb_info *info, int y, int sx, int dx, int width) {}
static inline void fbcon_decor_fix_pseudo_pal(struct fb_info *info, struct vc_data *vc) {}
static inline int fbcon_decor_call_helper(char* cmd, unsigned short cons) { return 0; }
static inline int fbcon_decor_init(void) { return 0; }
static inline int fbcon_decor_exit(void) { return 0; }
static inline int fbcon_decor_disable(struct vc_data *vc, unsigned char redraw) { return 0; }

#define fbcon_decor_active_vc(y) (0)
#define fbcon_decor_active_nores(x,y) (0)
#define fbcon_decor_active(x,y) (0)

#endif /* CONFIG_FB_CON_DECOR */

#endif /* __FBCON_DECOR_H */
