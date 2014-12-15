#ifndef _OSD_HW_DEF_H
#define	_OSD_HW_DEF_H
#include <linux/amlogic/amports/vframe_provider.h>
#include <plat/fiq_bridge.h>
#include "osd_hw.h"

/************************************************************************
**
**	macro  define  part
**
**************************************************************************/
#define	LEFT		0
#define	RIGHT		1
#define	OSD_RELATIVE_BITS               0x333f0
#define HW_OSD_COUNT                    2
#define HW_OSD_BLOCK_COUNT              4
#define HW_OSD_BLOCK_REG_COUNT          (HW_OSD_BLOCK_COUNT*2)
#define HW_OSD_BLOCK_ENABLE_MASK        0x000F
#define HW_OSD_BLOCK_ENABLE_0           0x0001 /* osd blk0 enable */
#define HW_OSD_BLOCK_ENABLE_1           0x0002 /* osd blk1 enable */
#define HW_OSD_BLOCK_ENABLE_2           0x0004 /* osd blk2 enable */
#define HW_OSD_BLOCK_ENABLE_3           0x0008 /* osd blk3 enable */
#define HW_OSD_BLOCK_LAYOUT_MASK        0xFFFF0000
/*
 * osd block layout horizontal:
 * -------------
 * |     0     |
 * |-----------|
 * |     1     |
 * |-----------|
 * |     2     |
 * |-----------|
 * |     3     |
 * -------------
 */
#define HW_OSD_BLOCK_LAYOUT_HORIZONTAL 0x10000
/*
 * osd block layout vertical:
 * -------------
 * |  |  |  |  |
 * |  |  |  |  |
 * | 0| 1| 2| 3|
 * |  |  |  |  |
 * |  |  |  |  |
 * -------------
 *
 * NOTE:
 *     In this mode, just one of the OSD blocks can be enabled at the same time.
 *     Because the blocks must be sequenced in vertical display order if they
 *     want to be both enabled at the same time.
 */
#define HW_OSD_BLOCK_LAYOUT_VERTICAL 0x20000
/*
 * osd block layout grid:
 * -------------
 * |     |     |
 * |  0  |  1  |
 * |-----|-----|
 * |     |     |
 * |  2  |  3  |
 * -------------
 *
 * NOTE:
 *     In this mode, Block0 and Block1 cannot be enabled at the same time.
 *     Neither can Block2 and Block3.
 */
#define HW_OSD_BLOCK_LAYOUT_GRID 0x30000
/*
 * osd block layout customer, need setting block_windows
 */
#define HW_OSD_BLOCK_LAYOUT_CUSTOMER 0xFFFF0000


/************************************************************************
**
**	typedef  define  part
**
**************************************************************************/
typedef void (*update_func_t)(void);

typedef struct {
	struct list_head list;
	update_func_t update_func;  //each reg group has it's own update function.
} hw_list_t;

typedef struct {
	u32 width;  //in byte unit
	u32	height;
	u32 canvas_idx;
	u32	addr;
} fb_geometry_t;

typedef struct {
	u16	h_enable;
	u16	v_enable;
} osd_ext_scale_t;

typedef  struct{
	u16	hfs_enable;
	u16	vfs_enable;
}osd_ext_freescale_t;

typedef struct {
	osd_ext_scale_t origin_scale;
	u16 enable;
	u16 left_right;
	u16 l_start;
	u16 l_end;
	u16 r_start;
	u16 r_end;
} osd_ext_3d_mode_t;

typedef struct{
	u32  on_off;
	u32  angle;
}osd_rotate_t;

typedef pandata_t dispdata_t;

typedef struct {
	pandata_t       pandata[HW_OSD_COUNT];
	dispdata_t      dispdata[HW_OSD_COUNT];
	pandata_t       scaledata[HW_OSD_COUNT];
	pandata_t 	free_scale_data[HW_OSD_COUNT];
	pandata_t	free_dst_data[HW_OSD_COUNT];
	u32             gbl_alpha[HW_OSD_COUNT];
	u32             color_key[HW_OSD_COUNT];
	u32             color_key_enable[HW_OSD_COUNT];
	u32             enable[HW_OSD_COUNT];
	u32             reg_status_save;
	bridge_item_t   fiq_handle_item;
	osd_ext_scale_t scale[HW_OSD_COUNT];
	osd_ext_freescale_t free_scale[HW_OSD_COUNT];
	u32             free_scale_enable[HW_OSD_COUNT];
	u32             free_scale_width[HW_OSD_COUNT];
	u32             free_scale_height[HW_OSD_COUNT];
	fb_geometry_t   fb_gem[HW_OSD_COUNT];
	const color_bit_define_t *color_info[HW_OSD_COUNT];
	u32             scan_mode;
	u32             osd_ext_order;
	osd_ext_3d_mode_t mode_3d[HW_OSD_COUNT];
	u32             updated[HW_OSD_COUNT];
	u32             block_windows[HW_OSD_COUNT][HW_OSD_BLOCK_REG_COUNT];
	u32             block_mode[HW_OSD_COUNT];
	u32		free_scale_mode[HW_OSD_COUNT];
	u32		osd_reverse[HW_OSD_COUNT];
	osd_rotate_t	rotate[HW_OSD_COUNT];
	pandata_t		rotation_pandata[HW_OSD_COUNT];
	hw_list_t       reg[HW_OSD_COUNT][HW_REG_INDEX_MAX];
	u32             clone[HW_OSD_COUNT];
	u32             angle[HW_OSD_COUNT];
} hw_para_t;

/************************************************************************
**
**	func declare  part
**
**************************************************************************/
static void osd1_update_color_mode(void);
static void osd1_update_enable(void);
static void osd1_update_color_key(void);
static void osd1_update_color_key_enable(void);
static void osd1_update_gbl_alpha(void);
static void osd1_update_order(void);
static void osd1_update_coef(void);
static void osd1_update_disp_geometry(void);
static void osd1_update_disp_freescale_enable(void);
static void osd1_update_disp_osd_rotate(void);
static void osd1_update_disp_scale_enable(void);
static void osd1_update_disp_3d_mode(void);

static void osd2_update_color_mode(void);
static void osd2_update_enable(void);
static void osd2_update_color_key_enable(void);
static void osd2_update_color_key(void);
static void osd2_update_gbl_alpha(void);
static void osd2_update_order(void);
static void osd2_update_coef(void);
static void osd2_update_disp_geometry(void);
static void osd2_update_disp_freescale_enable(void);
static void osd2_update_disp_osd_rotate(void);
static void osd2_update_disp_scale_enable(void);
static void osd2_update_disp_3d_mode(void);

/************************************************************************
**
**	global varible  define  part
**
**************************************************************************/
static DEFINE_SPINLOCK(osd_ext_lock);
static hw_para_t osd_ext_hw;
static unsigned long lock_flags;
#ifdef FIQ_VSYNC
static unsigned long fiq_flag;
#endif
static vframe_t vf;
static update_func_t hw_func_array[HW_OSD_COUNT][HW_REG_INDEX_MAX] = {
	{
		osd1_update_color_mode,
		osd1_update_enable,
		osd1_update_color_key,
		osd1_update_color_key_enable,
		osd1_update_gbl_alpha,
		osd1_update_order,
		osd1_update_coef,
		osd1_update_disp_geometry,
		osd1_update_disp_scale_enable,
		osd1_update_disp_freescale_enable,
		osd1_update_disp_osd_rotate,
	},
	{
		osd2_update_color_mode,
		osd2_update_enable,
		osd2_update_color_key,
		osd2_update_color_key_enable,
		osd2_update_gbl_alpha,
		osd2_update_order,
		osd2_update_coef,
		osd2_update_disp_geometry,
		osd2_update_disp_scale_enable,
		osd2_update_disp_freescale_enable,
		osd2_update_disp_osd_rotate,
	},
};

#ifdef FIQ_VSYNC
#define add_to_update_list(osd_ext_idx,cmd_idx) \
	spin_lock_irqsave(&osd_ext_lock, lock_flags); \
	raw_local_save_flags(fiq_flag); \
	local_fiq_disable(); \
	osd_ext_hw.updated[osd_ext_idx]|=(1<<cmd_idx); \
	raw_local_irq_restore(fiq_flag); \
	spin_unlock_irqrestore(&osd_ext_lock, lock_flags);
#else
#define add_to_update_list(osd_ext_idx,cmd_idx) \
	spin_lock_irqsave(&osd_ext_lock, lock_flags); \
	osd_ext_hw.updated[osd_ext_idx]|=(1<<cmd_idx); \
	spin_unlock_irqrestore(&osd_ext_lock, lock_flags);
#endif

#define remove_from_update_list(osd_ext_idx,cmd_idx) \
	osd_ext_hw.updated[osd_ext_idx]&=~(1<<cmd_idx);
#endif
