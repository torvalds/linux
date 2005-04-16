#define NULL 0
//typedef unsigned int size_t; 


struct screen_info {
	unsigned char  orig_x;			/* 0x00 */
	unsigned char  orig_y;			/* 0x01 */
	unsigned short dontuse1;		/* 0x02 -- EXT_MEM_K sits here */
	unsigned short orig_video_page;		/* 0x04 */
	unsigned char  orig_video_mode;		/* 0x06 */
	unsigned char  orig_video_cols;		/* 0x07 */
	unsigned short unused2;			/* 0x08 */
	unsigned short orig_video_ega_bx;	/* 0x0a */
	unsigned short unused3;			/* 0x0c */
	unsigned char  orig_video_lines;	/* 0x0e */
	unsigned char  orig_video_isVGA;	/* 0x0f */
	unsigned short orig_video_points;	/* 0x10 */

	/* VESA graphic mode -- linear frame buffer */
	unsigned short lfb_width;		/* 0x12 */
	unsigned short lfb_height;		/* 0x14 */
	unsigned short lfb_depth;		/* 0x16 */
	unsigned long  lfb_base;		/* 0x18 */
	unsigned long  lfb_size;		/* 0x1c */
	unsigned short dontuse2, dontuse3;	/* 0x20 -- CL_MAGIC and CL_OFFSET here */
	unsigned short lfb_linelength;		/* 0x24 */
	unsigned char  red_size;		/* 0x26 */
	unsigned char  red_pos;			/* 0x27 */
	unsigned char  green_size;		/* 0x28 */
	unsigned char  green_pos;		/* 0x29 */
	unsigned char  blue_size;		/* 0x2a */
	unsigned char  blue_pos;		/* 0x2b */
	unsigned char  rsvd_size;		/* 0x2c */
	unsigned char  rsvd_pos;		/* 0x2d */
	unsigned short vesapm_seg;		/* 0x2e */
	unsigned short vesapm_off;		/* 0x30 */
	unsigned short pages;			/* 0x32 */
						/* 0x34 -- 0x3f reserved for future expansion */
};
