/* SPDX-License-Identifier: GPL-2.0 */
#ifndef STICORE_H
#define STICORE_H

/* generic STI structures & functions */

#define MAX_STI_ROMS 4		/* max no. of ROMs which this driver handles */

#define STI_REGION_MAX 8	/* hardcoded STI constants */
#define STI_DEV_NAME_LENGTH 32
#define STI_MONITOR_MAX 256

#define STI_FONT_HPROMAN8 1
#define STI_FONT_KANA8 2

#define ALT_CODE_TYPE_UNKNOWN 0x00	/* alt code type values */
#define ALT_CODE_TYPE_PA_RISC_64 0x01

/* The latency of the STI functions cannot really be reduced by setting
 * this to 0;  STI doesn't seem to be designed to allow calling a different
 * function (or the same function with different arguments) after a
 * function exited with 1 as return value.
 *
 * As all of the functions below could be called from interrupt context,
 * we have to spin_lock_irqsave around the do { ret = bla(); } while(ret==1)
 * block.  Really bad latency there.
 *
 * Probably the best solution to all this is have the generic code manage
 * the screen buffer and a kernel thread to call STI occasionally.
 * 
 * Luckily, the frame buffer guys have the same problem so we can just wait
 * for them to fix it and steal their solution.   prumpf
 */
 
#include <asm/io.h>

#define STI_WAIT 1

#define STI_PTR(p)	( virt_to_phys(p) )
#define PTR_STI(p)	( phys_to_virt((unsigned long)p) )

#define sti_onscreen_x(sti) (sti->glob_cfg->onscreen_x)
#define sti_onscreen_y(sti) (sti->glob_cfg->onscreen_y)

/* sti_font_xy() use the native font ROM ! */
#define sti_font_x(sti) (PTR_STI(sti->font)->width)
#define sti_font_y(sti) (PTR_STI(sti->font)->height)

#ifdef CONFIG_64BIT
#define STI_LOWMEM	(GFP_KERNEL | GFP_DMA)
#else
#define STI_LOWMEM	(GFP_KERNEL)
#endif


/* STI function configuration structs */

typedef union region {
	struct { 
		u32 offset	: 14;	/* offset in 4kbyte page */
		u32 sys_only	: 1;	/* don't map to user space */
		u32 cache	: 1;	/* map to data cache */
		u32 btlb	: 1;	/* map to block tlb */
		u32 last	: 1;	/* last region in list */
		u32 length	: 14;	/* length in 4kbyte page */
	} region_desc;

	u32 region;			/* complete region value */
} region_t;

#define REGION_OFFSET_TO_PHYS( rt, hpa ) \
	(((rt).region_desc.offset << 12) + (hpa))

struct sti_glob_cfg_ext {
	 u8 curr_mon;			/* current monitor configured */
	 u8 friendly_boot;		/* in friendly boot mode */
	s16 power;			/* power calculation (in Watts) */
	s32 freq_ref;			/* frequency reference */
	u32 sti_mem_addr;		/* pointer to global sti memory (size=sti_mem_request) */
	u32 future_ptr; 		/* pointer to future data */
};

struct sti_glob_cfg {
	s32 text_planes;		/* number of planes used for text */
	s16 onscreen_x;			/* screen width in pixels */
	s16 onscreen_y;			/* screen height in pixels */
	s16 offscreen_x;		/* offset width in pixels */
	s16 offscreen_y;		/* offset height in pixels */
	s16 total_x;			/* frame buffer width in pixels */
	s16 total_y;			/* frame buffer height in pixels */
	u32 region_ptrs[STI_REGION_MAX]; /* region pointers */
	s32 reent_lvl;			/* storage for reentry level value */
	u32 save_addr;			/* where to save or restore reentrant state */
	u32 ext_ptr;			/* pointer to extended glob_cfg data structure */
};


/* STI init function structs */

struct sti_init_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 reset : 1;		/* hard reset the device? */
	u32 text : 1;		/* turn on text display planes? */
	u32 nontext : 1;	/* turn on non-text display planes? */
	u32 clear : 1;		/* clear text display planes? */
	u32 cmap_blk : 1;	/* non-text planes cmap black? */
	u32 enable_be_timer : 1; /* enable bus error timer */
	u32 enable_be_int : 1;	/* enable bus error timer interrupt */
	u32 no_chg_tx : 1;	/* don't change text settings */
	u32 no_chg_ntx : 1;	/* don't change non-text settings */
	u32 no_chg_bet : 1;	/* don't change berr timer settings */
	u32 no_chg_bei : 1;	/* don't change berr int settings */
	u32 init_cmap_tx : 1;	/* initialize cmap for text planes */
	u32 cmt_chg : 1;	/* change current monitor type */
	u32 retain_ie : 1;	/* don't allow reset to clear int enables */
	u32 caller_bootrom : 1;	/* set only by bootrom for each call */
	u32 caller_kernel : 1;	/* set only by kernel for each call */
	u32 caller_other : 1;	/* set only by non-[BR/K] caller */
	u32 pad	: 14;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_init_inptr_ext {
	u8  config_mon_type;	/* configure to monitor type */
	u8  pad[1];		/* pad to word boundary */
	u16 inflight_data;	/* inflight data possible on PCI */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_init_inptr {
	s32 text_planes;	/* number of planes to use for text */
	u32 ext_ptr;		/* pointer to extended init_graph inptr data structure*/
};


struct sti_init_outptr {
	s32 errno;		/* error number on failure */
	s32 text_planes;	/* number of planes used for text */
	u32 future_ptr; 	/* pointer to future data */
};



/* STI configuration function structs */

struct sti_conf_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 pad : 31;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_conf_inptr {
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_conf_outptr_ext {
	u32 crt_config[3];	/* hardware specific X11/OGL information */	
	u32 crt_hdw[3];
	u32 future_ptr;
};

struct sti_conf_outptr {
	s32 errno;		/* error number on failure */
	s16 onscreen_x;		/* screen width in pixels */
	s16 onscreen_y;		/* screen height in pixels */
	s16 offscreen_x;	/* offscreen width in pixels */
	s16 offscreen_y;	/* offscreen height in pixels */
	s16 total_x;		/* frame buffer width in pixels */
	s16 total_y;		/* frame buffer height in pixels */
	s32 bits_per_pixel;	/* bits/pixel device has configured */
	s32 bits_used;		/* bits which can be accessed */
	s32 planes;		/* number of fb planes in system */
	 u8 dev_name[STI_DEV_NAME_LENGTH]; /* null terminated product name */
	u32 attributes;		/* flags denoting attributes */
	u32 ext_ptr;		/* pointer to future data */
};

struct sti_rom {
	 u8 type[4];
	 u8 res004;
	 u8 num_mons;
	 u8 revno[2];
	u32 graphics_id[2];

	u32 font_start;
	u32 statesize;
	u32 last_addr;
	u32 region_list;

	u16 reentsize;
	u16 maxtime;
	u32 mon_tbl_addr;
	u32 user_data_addr;
	u32 sti_mem_req;

	u32 user_data_size;
	u16 power;
	 u8 bus_support;
	 u8 ext_bus_support;
	 u8 alt_code_type;
	 u8 ext_dd_struct[3];
	u32 cfb_addr;

	u32 init_graph;
	u32 state_mgmt;
	u32 font_unpmv;
	u32 block_move;
	u32 self_test;
	u32 excep_hdlr;
	u32 inq_conf;
	u32 set_cm_entry;
	u32 dma_ctrl;
	 u8 res040[7 * 4];
	
	u32 init_graph_addr;
	u32 state_mgmt_addr;
	u32 font_unp_addr;
	u32 block_move_addr;
	u32 self_test_addr;
	u32 excep_hdlr_addr;
	u32 inq_conf_addr;
	u32 set_cm_entry_addr;
	u32 image_unpack_addr;
	u32 pa_risx_addrs[7];
};

struct sti_rom_font {
	u16 first_char;
	u16 last_char;
	 u8 width;
	 u8 height;
	 u8 font_type;		/* language type */
	 u8 bytes_per_char;
	u32 next_font;
	 u8 underline_height;
	 u8 underline_pos;
	 u8 res008[2];
};

/* sticore internal font handling */

struct sti_cooked_font {
	struct sti_rom_font *raw;	/* native ptr for STI functions */
	void *raw_ptr;			/* kmalloc'ed font data */
	struct sti_cooked_font *next_font;
	int height, width;
	int refcount;
	u32 crc;
};

struct sti_cooked_rom {
        struct sti_rom *raw;
	struct sti_cooked_font *font_start;
};

/* STI font printing function structs */

struct sti_font_inptr {
	u32 font_start_addr;	/* address of font start */
	s16 index;		/* index into font table of character */
	u8 fg_color;		/* foreground color of character */
	u8 bg_color;		/* background color of character */
	s16 dest_x;		/* X location of character upper left */
	s16 dest_y;		/* Y location of character upper left */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_font_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 non_text : 1;	/* font unpack/move in non_text planes =1, text =0 */
	u32 pad : 30;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};
	
struct sti_font_outptr {
	s32 errno;		/* error number on failure */
	u32 future_ptr; 	/* pointer to future data */
};

/* STI blockmove structs */

struct sti_blkmv_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 color : 1;		/* change color during move? */
	u32 clear : 1;		/* clear during move? */
	u32 non_text : 1;	/* block move in non_text planes =1, text =0 */
	u32 pad : 28;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_blkmv_inptr {
	u8 fg_color;		/* foreground color after move */
	u8 bg_color;		/* background color after move */
	s16 src_x;		/* source upper left pixel x location */
	s16 src_y;		/* source upper left pixel y location */
	s16 dest_x;		/* dest upper left pixel x location */
	s16 dest_y;		/* dest upper left pixel y location */
	s16 width;		/* block width in pixels */
	s16 height;		/* block height in pixels */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_blkmv_outptr {
	s32 errno;		/* error number on failure */
	u32 future_ptr; 	/* pointer to future data */
};


/* sti_all_data is an internal struct which needs to be allocated in
 * low memory (< 4GB) if STI is used with 32bit STI on a 64bit kernel */

struct sti_all_data {
	struct sti_glob_cfg glob_cfg;
	struct sti_glob_cfg_ext glob_cfg_ext;

	struct sti_conf_inptr		inq_inptr;
	struct sti_conf_outptr		inq_outptr; /* configuration */
	struct sti_conf_outptr_ext	inq_outptr_ext;

	struct sti_init_inptr_ext	init_inptr_ext;
	struct sti_init_inptr		init_inptr;
	struct sti_init_outptr		init_outptr;

	struct sti_blkmv_inptr		blkmv_inptr;
	struct sti_blkmv_outptr		blkmv_outptr;

	struct sti_font_inptr		font_inptr;
	struct sti_font_outptr		font_outptr;

	/* leave as last entries */
	unsigned long save_addr[1024 / sizeof(unsigned long)];
	   /* min 256 bytes which is STI default, max sti->sti_mem_request */
	unsigned long sti_mem_addr[256 / sizeof(unsigned long)];
	/* do not add something below here ! */
};

/* internal generic STI struct */

struct sti_struct {
	spinlock_t lock;
		
	/* char **mon_strings; */
	int sti_mem_request;
	u32 graphics_id[2];

	struct sti_cooked_rom *rom;

	unsigned long font_unpmv;
	unsigned long block_move;
	unsigned long init_graph;
	unsigned long inq_conf;

	/* all following fields are initialized by the generic routines */
	int text_planes;
	region_t regions[STI_REGION_MAX];
	unsigned long regions_phys[STI_REGION_MAX];

	struct sti_glob_cfg *glob_cfg;	/* points into sti_all_data */

	int wordmode;
	struct sti_cooked_font *font;	/* ptr to selected font (cooked) */

	struct pci_dev *pd;

	/* PCI data structures (pg. 17ff from sti.pdf) */
	u8 rm_entry[16]; /* pci region mapper array == pci config space offset */

	/* pointer to the fb_info where this STI device is used */
	struct fb_info *info;

	/* pointer to all internal data */
	struct sti_all_data *sti_data;
};


/* sticore interface functions */

struct sti_struct *sti_get_rom(unsigned int index); /* 0: default sti */
void sti_font_convert_bytemode(struct sti_struct *sti, struct sti_cooked_font *f);


/* sticore main function to call STI firmware */

int sti_call(const struct sti_struct *sti, unsigned long func,
		const void *flags, void *inptr, void *outptr,
		struct sti_glob_cfg *glob_cfg);


/* functions to call the STI ROM directly */

void sti_putc(struct sti_struct *sti, int c, int y, int x,
		struct sti_cooked_font *font);
void sti_set(struct sti_struct *sti, int src_y, int src_x,
		int height, int width, u8 color);
void sti_clear(struct sti_struct *sti, int src_y, int src_x,
		int height, int width, int c, struct sti_cooked_font *font);
void sti_bmove(struct sti_struct *sti, int src_y, int src_x,
		int dst_y, int dst_x, int height, int width,
		struct sti_cooked_font *font);

#endif	/* STICORE_H */
