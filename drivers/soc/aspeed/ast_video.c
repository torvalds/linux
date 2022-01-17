// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2021 Aspeed Technology Inc.

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/reset.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/hwmon-sysfs.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <asm/uaccess.h>
//#include <linux/aspeed-sdmc.h>
//#include <linux/ast_lcd.h>
#include <linux/fb.h>

/***********************************************************************/
/* Register for VIDEO */
#define AST_VIDEO_PROTECT		0x000		/*	protection key register	*/
#define AST_VIDEO_SEQ_CTRL		0x004		/*	Video Sequence Control register	*/
#define AST_VIDEO_PASS_CTRL		0x008		/*	Video Pass 1 Control register	*/

//VR008[5]=1
#define AST_VIDEO_DIRECT_BASE	0x00C		/*	Video Direct Frame buffer mode control Register VR008[5]=1 */
#define AST_VIDEO_DIRECT_CTRL	0x010		/*	Video Direct Frame buffer mode control Register VR008[5]=1 */

//VR008[5]=0
#define AST_VIDEO_TIMING_H		0x00C		/*	Video Timing Generation Setting Register */
#define AST_VIDEO_TIMING_V		0x010		/*	Video Timing Generation Setting Register */
#define AST_VIDEO_SCAL_FACTOR	0x014		/*	Video Scaling Factor Register */

#define AST_VIDEO_SCALING0		0x018		/*	Video Scaling Filter Parameter Register #0 */
#define AST_VIDEO_SCALING1		0x01C		/*	Video Scaling Filter Parameter Register #1 */
#define AST_VIDEO_SCALING2		0x020		/*	Video Scaling Filter Parameter Register #2 */
#define AST_VIDEO_SCALING3		0x024		/*	Video Scaling Filter Parameter Register #3 */

#define AST_VIDEO_BCD_CTRL		0x02C		/*	Video BCD Control Register */
#define AST_VIDEO_CAPTURE_WIN	0x030		/*	 Video Capturing Window Setting Register */
#define AST_VIDEO_COMPRESS_WIN	0x034		/*	 Video Compression Window Setting Register */


#define AST_VIDEO_COMPRESS_PRO	0x038		/* Video Compression Stream Buffer Processing Offset Register */
#define AST_VIDEO_COMPRESS_READ	0x03C		/* Video Compression Stream Buffer Read Offset Register */

#define AST_VIDEO_JPEG_HEADER_BUFF		0x040		/*	Video Based Address of JPEG Header Buffer Register */
#define AST_VIDEO_SOURCE_BUFF0	0x044		/*	Video Based Address of Video Source Buffer #1 Register */
#define AST_VIDEO_SOURCE_SCAN_LINE	0x048		/*	Video Scan Line Offset of Video Source Buffer Register */
#define AST_VIDEO_SOURCE_BUFF1	0x04C		/*	Video Based Address of Video Source Buffer #2 Register */
#define AST_VIDEO_BCD_BUFF		0x050		/*	Video Base Address of BCD Flag Buffer Register */
#define AST_VIDEO_STREAM_BUFF	0x054		/*	Video Base Address of Compressed Video Stream Buffer Register */
#define AST_VIDEO_STREAM_SIZE	0x058		/*	Video Stream Buffer Size Register */

#define AST_VIDEO_COMPRESS_CTRL	0x060		/* Video Compression Control Register */


#define AST_VIDEO_COMPRESS_DATA_COUNT		0x070		/* Video Total Size of Compressed Video Stream Read Back Register */
#define AST_VIDEO_COMPRESS_BLOCK_COUNT		0x074		/* Video Total Number of Compressed Video Block Read Back Register */
#define AST_VIDEO_COMPRESS_FRAME_END		0x078		/* Video Frame-end offset of compressed video stream buffer read back Register */



#define AST_VIDEO_DEF_HEADER	0x080		/* Video User Defined Header Parameter Setting with Compression */
#define AST_VIDEO_JPEG_COUNT	0x084		/* true jpeg size */

#define AST_VIDEO_H_DETECT_STS  0x090		/* Video Source Left/Right Edge Detection Read Back Register */
#define AST_VIDEO_V_DETECT_STS  0x094		/* Video Source Top/Bottom Edge Detection Read Back Register */


#define AST_VIDEO_MODE_DET_STS	0x098		/* Video Mode Detection Status Read Back Register */

#define AST_VIDEO_MODE_DET1		0x0A4		/* Video Mode Detection Control Register 1*/
#define AST_VIDEO_MODE_DET2		0x0A8		/* Video Mode Detection Control Register 2*/


#define AST_VIDEO_BONDING_X		0x0D4
#define AST_VIDEO_BONDING_Y		0x0D8

#define AST_VM_SEQ_CTRL			0x204		/* Video Management Control Sequence Register */
#define AST_VM_PASS_CTRL			0x208		/* Video Management Pass 1 Control register	*/
#define AST_VM_SCAL_FACTOR		0x214		/* Video Management Scaling Factor Register */
#define AST_VM_BCD_CTRL			0x22C		/* Video Management BCD Control Register */
#define AST_VM_CAPTURE_WIN		0x230		/* Video Management Capturing Window Setting Register */
#define AST_VM_COMPRESS_WIN		0x234		/* Video Management Compression Window Setting Register */
#define AST_VM_JPEG_HEADER_BUFF	0x240		/* Video Management Based Address of JPEG Header Buffer Register */
#define AST_VM_SOURCE_BUFF0		0x244		/* Video Management Based Address of Video Source Buffer Register */
#define AST_VM_SOURCE_SCAN_LINE	0x248		/* Video Management Scan Line Offset of Video Source Buffer Register */

#define AST_VM_COMPRESS_BUFF		0x254		/* Video Management Based Address of Compressed Video Buffer Register */
#define AST_VM_STREAM_SIZE			0x258		/* Video Management Buffer Size Register */
#define AST_VM_COMPRESS_CTRL			0x260		/* Video Management Compression or Video Profile 2-5 Decompression Control Register */
#define AST_VM_COMPRESS_VR264			0x264		/* VR264 REserved */
#define AST_VM_COMPRESS_BLOCK_COUNT		0x274		/* Video Total Number of Compressed Video Block Read Back Register */
#define AST_VM_COMPRESS_FRAME_END	0x278	/*16 bytes align */	/* Video Management Frame-end offset of compressed video stream buffer read back Register */


#define AST_VIDEO_CTRL			0x300		/* Video Control Register */
#define AST_VIDEO_INT_EN		0x304		/* Video interrupt Enable */
#define AST_VIDEO_INT_STS		0x308		/* Video interrupt status */
#define AST_VIDEO_MODE_DETECT	0x30C		/* Video Mode Detection Parameter Register */

#define AST_VIDEO_CRC1			0x320		/* Primary CRC Parameter Register */
#define AST_VIDEO_CRC2			0x324		/* Second CRC Parameter Register */
#define AST_VIDEO_DATA_TRUNCA	0x328		/* Video Data Truncation Register */


#define AST_VIDEO_SCRATCH_340	0x340		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_344	0x344		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_348	0x348		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_34C	0x34C		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_350	0x350		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_354	0x354		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_358	0x358		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_35C	0x35C		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_360	0x360		/* Video Scratch Remap Read Back */
#define AST_VIDEO_SCRATCH_364	0x364		/* Video Scratch Remap Read Back */


#define AST_VIDEO_ENCRYPT_SRAM	0x400		/* Video RC4/AES128 Encryption Key Register #0 ~ #63 */
#define AST_VIDEO_MULTI_JPEG_SRAM	(AST_VIDEO_ENCRYPT_SRAM)		/* Multi JPEG registers */

#define REG_32_BIT_SZ_IN_BYTES (sizeof(u32))

#define SET_FRAME_W_H(w, h) ((((u32) (h)) & 0x1fff) | ((((u32) (w)) & 0x1fff) << 13))
#define SET_FRAME_START_ADDR(addr) ((addr) & 0x7fffff80)

/////////////////////////////////////////////////////////////////////////////

/*	AST_VIDEO_PROTECT: 0x000  - protection key register */
#define VIDEO_PROTECT_UNLOCK			0x1A038AA8

/*	AST_VIDEO_SEQ_CTRL		0x004		Video Sequence Control register	*/
#define VIDEO_HALT_ENG_STS				(1 << 21)
#define VIDEO_COMPRESS_BUSY				(1 << 18)
#define VIDEO_CAPTURE_BUSY				(1 << 16)
#define VIDEO_HALT_ENG_TRIGGER			(1 << 12)
#define VIDEO_COMPRESS_FORMAT_MASK		(3 << 10)
#define VIDEO_GET_COMPRESS_FORMAT(x)		((x >> 10) & 0x3)   // 0 YUV444
#define VIDEO_COMPRESS_FORMAT(x)		(x << 10)	// 0 YUV444
#define YUV420		1

#define G5_VIDEO_COMPRESS_JPEG_MODE			(1 << 13)
#define VIDEO_YUV2RGB_DITHER_EN			(1 << 8)

#define VIDEO_COMPRESS_JPEG_MODE			(1 << 8)

//if bit 0 : 1
#define VIDEO_INPUT_MODE_CHG_WDT		(1 << 7)
#define VIDEO_INSERT_FULL_COMPRESS		(1 << 6)
#define VIDEO_AUTO_COMPRESS				(1 << 5)
#define VIDEO_COMPRESS_TRIGGER			(1 << 4)
#define VIDEO_CAPTURE_MULTI_FRAME		(1 << 3)
#define VIDEO_COMPRESS_FORCE_IDLE		(1 << 2)
#define VIDEO_CAPTURE_TRIGGER			(1 << 1)
#define VIDEO_DETECT_TRIGGER			(1 << 0)

/*	AST_VIDEO_PASS_CTRL			0x008		Video Pass1 Control register	*/
#define G6_VIDEO_MULTI_JPEG_FLAG_MODE	(1 << 31)
#define G6_VIDEO_MULTI_JPEG_MODE		(1 << 30)
#define G6_VIDEO_JPEG__COUNT(x)			((x) << 24)
#define G6_VIDEO_FRAME_CT_MASK			(0x3f << 24)
//x * source frame rate / 60
#define VIDEO_FRAME_RATE_CTRL(x)		(x << 16)
#define VIDEO_HSYNC_POLARITY_CTRL		(1 << 15)
#define VIDEO_INTERLANCE_MODE			(1 << 14)
#define VIDEO_DUAL_EDGE_MODE			(1 << 13)	//0 : Single edage
#define VIDEO_18BIT_SINGLE_EDGE			(1 << 12)	//0: 24bits
#define VIDEO_DVO_INPUT_DELAY_MASK		(7 << 9)
#define VIDEO_DVO_INPUT_DELAY(x)		(x << 9) //0 : no delay , 1: 1ns, 2: 2ns, 3:3ns, 4: inversed clock but no delay
// if biit 5 : 0
#define VIDEO_HW_CURSOR_DIS				(1 << 8)
// if biit 5 : 1
#define VIDEO_AUTO_FATCH					(1 << 8)	//
#define VIDEO_CAPTURE_FORMATE_MASK		(3 << 6)

#define VIDEO_SET_CAPTURE_FORMAT(x)			(x << 6)
#define JPEG_MODE		1
#define RGB_MODE		2
#define GRAY_MODE		3
#define VIDEO_DIRT_FATCH				(1 << 5)
// if biit 5 : 0
#define VIDEO_INTERNAL_DE				(1 << 4)
#define VIDEO_EXT_ADC_ATTRIBUTE			(1 << 3)

// if biit 5 : 1
#define VIDEO_16BPP_MODE				(1 << 4)
#define VIDEO_16BPP_MODE_555			(1 << 3)	//0:565

#define VIDEO_FROM_EXT_SOURCE			(1 << 2)
#define VIDEO_SO_VSYNC_POLARITY			(1 << 1)
#define VIDEO_SO_HSYNC_POLARITY			(1 << 0)

/*	AST_VIDEO_TIMING_H		0x00C		Video Timing Generation Setting Register */
#define VIDEO_HSYNC_PIXEL_FIRST_SET(x)	(((x) & 0xfff) << 16)
#define VIDEO_HSYNC_PIXEL_FIRST_MASK	0xFFFF0000
#define VIDEO_HSYNC_PIXEL_LAST_SET(x)	((x) & 0xfff)
#define VIDEO_HSYNC_PIXEL_LAST_MASK	0x0000FFFF

/*	AST_VIDEO_DIRECT_CTRL	0x010		Video Direct Frame buffer mode control Register VR008[5]=1 */
#define VIDEO_FETCH_TIMING(x)			((x) << 16)
#define VIDEO_FETCH_LINE_OFFSET(x)		(x & 0xffff)

/*	AST_VIDEO_TIMING_V		0x010		Video Timing Generation Setting Register */
#define VIDEO_VSYNC_PIXEL_FIRST_SET(x)	((x) << 16)
#define VIDEO_VSYNC_PIXEL_LAST_SET(x)	(x)

/*	AST_VIDEO_SCAL_FACTOR	0x014		Video Scaling Factor Register */
#define VIDEO_V_SCAL_FACTOR(x)			(((x) & 0xffff) << 16)
#define VIDEO_V_SCAL_FACTOR_MASK		(0x0000ffff)
#define VIDEO_H_SCAL_FACTOR(x)			((x) & 0xffff)
#define VIDEO_H_SCAL_FACTOR_MASK		(0xffff0000)

/*	AST_VIDEO_SCALING0		0x018		Video Scaling Filter Parameter Register #0 */
/*	AST_VIDEO_SCALING1		0x01C		Video Scaling Filter Parameter Register #1 */
/*	AST_VIDEO_SCALING2		0x020		Video Scaling Filter Parameter Register #2 */
/*	AST_VIDEO_SCALING3		0x024		Video Scaling Filter Parameter Register #3 */


/*	AST_VIDEO_BCD_CTRL		0x02C		Video BCD Control Register */
#define VIDEO_SET_ABCD_TOL(x)			((x & 0xff) << 24)
#define VIDEO_GET_ABCD_TOL(x)			((x >> 24) & 0xff)
#define VIDEO_SET_BCD_TOL(x)			((x & 0xff) << 16)
#define VIDEO_GET_BCD_TOL(x)			((x >> 16) & 0xff)

#define VIDEO_ABCD_CHG_EN				(1 << 1)
#define VIDEO_BCD_CHG_EN				(1)

/*	 AST_VIDEO_CAPTURE_WIN	0x030		Video Capturing Window Setting Register */
#define VIDEO_CAPTURE_V(x)				(x & 0x7ff)
#define VIDEO_CAPTURE_H(x)				((x & 0x7ff) << 16)

/*	 AST_VIDEO_COMPRESS_WIN	0x034		Video Compression Window Setting Register */
#define VIDEO_COMPRESS_V(x)				(x & 0x7ff)
#define VIDEO_GET_COMPRESS_V(x)			(x & 0x7ff)
#define VIDEO_COMPRESS_H(x)				((x & 0x7ff) << 16)
#define VIDEO_GET_COMPRESS_H(x)			((x >> 16) & 0x7ff)

/*	AST_VIDEO_RESET :0x03c	 - system reset control register */

/*	AST_VIDEO_STREAM_SIZE	0x058		Video Stream Buffer Size Register */
#define VIDEO_STREAM_PKT_N(x)			(x << 3)
#define STREAM_4_PKTS		0
#define STREAM_8_PKTS		1
#define STREAM_16_PKTS		2
#define STREAM_32_PKTS		3
#define STREAM_64_PKTS		4
#define STREAM_128_PKTS		5

#define VIDEO_STREAM_PKT_SIZE(x)		(x)
#define STREAM_1KB		0
#define STREAM_2KB		1
#define STREAM_4KB		2
#define STREAM_8KB		3
#define STREAM_16KB		4
#define STREAM_32KB		5
#define STREAM_64KB		6
#define STREAM_128KB	7

/* AST_VIDEO_COMPRESS_CTRL	0x060		Video Compression Control Register */
#define VIDEO_HQ_DCT_LUM(x)				((x) << 27)
#define VIDEO_GET_HQ_DCT_LUM(x)			((x >> 27) & 0x1f)
#define VIDEO_HQ_DCT_CHROM(x)				((x) << 22)
#define VIDEO_GET_HQ_DCT_CHROM(x)			((x >> 22) & 0x1f)
#define VIDEO_HQ_DCT_MASK					(0x3ff << 22)
#define VIDEO_DCT_HUFFMAN_ENCODE(x)		((x) << 20)
#define VIDEO_DCT_RESET						(1 << 17)
#define VIDEO_HQ_ENABLE					(1 << 16)
#define VIDEO_GET_HQ_ENABLE(x)				((x >> 16) & 0x1)
#define VIDEO_DCT_LUM(x)					((x) << 11)
#define VIDEO_GET_DCT_LUM(x)				((x >> 11) & 0x1f)
#define VIDEO_DCT_CHROM(x)					((x) << 6)
#define VIDEO_GET_DCT_CHROM(x)			((x >> 6) & 0x1f)
#define VIDEO_DCT_MASK						(0x3ff << 6)
#define VIDEO_ENCRYP_ENABLE				(1 << 5)
#define VIDEO_COMPRESS_QUANTIZ_MODE		(1 << 2)
#define VIDEO_4COLOR_VQ_ENCODE			(1 << 1)
#define VIDEO_DCT_ONLY_ENCODE				(1)
#define VIDEO_DCT_VQ_MASK					(0x3)

/* AST_VIDEO_COMPRESS_BLOCK_COUNT - 0x074		Video Total Number of Compressed Video Block Read Back Register */
#define GET_BLOCK_CHG(x)				((x >> 16) & 0xffff)

/* AST_VIDEO_H_DETECT_STS  0x090		Video Source Left/Right Edge Detection Read Back Register */
#define VIDEO_DET_INTERLANCE_MODE		(1 << 31)
#define VIDEO_GET_HSYNC_RIGHT(x)		((x & 0x0FFF0000) >> 16)
#define VIDEO_GET_HSYNC_LEFT(x)			(x & 0xFFF)
#define VIDEO_NO_DISPLAY_CLOCK_DET		(1 << 15)
#define VIDEO_NO_ACT_DISPLAY_DET		(1 << 14)
#define VIDEO_NO_HSYNC_DET				(1 << 13)
#define VIDEO_NO_VSYNC_DET				(1 << 12)

/* AST_VIDEO_V_DETECT_STS  0x094		Video Source Top/Bottom Edge Detection Read Back Register */
#define VIDEO_GET_VSYNC_BOTTOM(x)		((x & 0x0FFF0000) >> 16)
#define VIDEO_GET_VSYNC_TOP(x)			(x & 0xFFF)


/* AST_VIDEO_MODE_DET_STS	0x098		Video Mode Detection Status Read Back Register */
#define VIDEO_DET_HSYNC_RDY				(1 << 31)
#define VIDEO_DET_VSYNC_RDY				(1 << 30)
#define VIDEO_DET_HSYNC_POLAR			(1 << 29)
#define VIDEO_DET_VSYNC_POLAR			(1 << 28)
#define VIDEO_GET_VER_SCAN_LINE(x)		((x >> 16) & 0xfff)
#define VIDEO_OUT_SYNC					(1 << 15)
#define VIDEO_DET_VER_STABLE			(1 << 14)
#define VIDEO_DET_HOR_STABLE			(1 << 13)
#define VIDEO_DET_FROM_ADC				(1 << 12)
#define VIDEO_DET_HOR_PERIOD(x)			(x & 0xfff)


/* AST_VIDEO_MODE_DET1		0x0A4		Video Mode Detection Control Register 1*/
#define VIDEO_DET_HSYNC_DELAY_MASK		(0xff << 16)
#define VIDEO_DET_LONG_H_STABLE_EN		(1 << 29)

/* AST_VM_SEQ_CTRL	0x204		Video Management Control Sequence Register */
#define VIDEO_VM_SET_YUV420				(1 << 10)
#define VIDEO_VM_JPEG_COMPRESS_MODE		(1 << 8)
#define VIDEO_VM_AUTO_COMPRESS			(1 << 5)
#define VIDEO_VM_COMPRESS_TRIGGER		(1 << 4)
#define VIDEO_VM_CAPTURE_TRIGGER			(1 << 1)

/* AST_VM_BUFF_SIZE			0x258		Video Management Buffer Size Register */
#define VM_STREAM_PKT_SIZE(x)		(x)
#define STREAM_1MB		0
#define STREAM_2MB		1
#define STREAM_3MB		2
#define STREAM_4MB		3

/* AST_VIDEO_CTRL			0x300		Video Control Register */
#define VIDEO_CTRL_CRYPTO(x)			(x << 17)
#define VIDEO_CTRL_CRYPTO_AES			(1 << 17)
#define VIDEO_CTRL_CRYPTO_FAST			(1 << 16)
#define VIDEO_CTRL_ADDRESS_MAP_MULTI_JPEG	(0x3 << 30)
//15 reserved
#define VIDEO_CTRL_RC4_VC				(1 << 14)
#define VIDEO_CTRL_CAPTURE_MASK			(3 << 12)
#define VIDEO_CTRL_CAPTURE_MODE(x)		(x << 12)
#define VIDEO_CTRL_COMPRESS_MASK		(3 << 10)
#define VIDEO_CTRL_COMPRESS_MODE(x)		(x << 10)
#define MODE_32BPP_YUV444		0
#define MODE_24BPP_YUV444		1
#define MODE_16BPP_YUV422		3

#define VIDEO_CTRL_RC4_TEST_MODE		(1 << 9)
#define VIDEO_CTRL_RC4_RST				(1 << 8)
#define VIDEO_CTRL_RC4_VIDEO_M_SEL		(1 << 7)		//video management
#define VIDEO_CTRL_RC4_VIDEO_2_SEL		(1 << 6)		// Video 2

#define VIDEO_CTRL_DWN_SCALING_MASK		(0x3 << 4)
#define VIDEO_CTRL_DWN_SCALING_ENABLE_LINE_BUFFER		(0x1 << 4)

#define VIDEO_CTRL_VSYNC_DELAY_MASK		(3 << 2)
#define VIDEO_CTRL_VSYNC_DELAY(x)		(x << 2)
#define NO_DELAY			0
#define DELAY_DIV12_HSYNC	1
#define AUTO_DELAY			2

/* AST_VIDEO_INT_EN			0x304		Video interrupt Enable */
/* AST_VIDEO_INT_STS		0x308		Video interrupt status */
#define VM_COMPRESS_COMPLETE			(1 << 17)
#define VM_CAPTURE_COMPLETE			(1 << 16)

#define VIDEO_FRAME_COMPLETE			(1 << 5)
#define VIDEO_MODE_DETECT_RDY			(1 << 4)
#define VIDEO_COMPRESS_COMPLETE			(1 << 3)
#define VIDEO_COMPRESS_PKT_COMPLETE		(1 << 2)
#define VIDEO_CAPTURE_COMPLETE			(1 << 1)
#define VIDEO_MODE_DETECT_WDT			(1 << 0)

/* AST_VIDEO_MODE_DETECT	0x30C		Video Mode Detection Parameter Register */
#define VIDEO_MODE_HOR_TOLER(x)			(x << 28)
#define VIDEO_MODE_VER_TOLER(x)			(x << 24)
#define VIDEO_MODE_HOR_STABLE(x)		(x << 20)
#define VIDEO_MODE_VER_STABLE(x)		(x << 16)
#define VIDEO_MODE_EDG_THROD(x)			(x << 8)

#define MODEDETECTION_VERTICAL_STABLE_MAXIMUM       0x6
#define MODEDETECTION_HORIZONTAL_STABLE_MAXIMUM     0x6
#define MODEDETECTION_VERTICAL_STABLE_THRESHOLD     0x2
#define MODEDETECTION_HORIZONTAL_STABLE_THRESHOLD   0x2

/* AST_VIDEO_SCRATCH_34C	0x34C		Video Scratch Remap Read Back */
#define SCRATCH_VGA_GET_REFLASH_RATE(x)			((x >> 8) & 0xf)
#define SCRATCH_VGA_GET_COLOR_MODE(x)			((x >> 4) & 0xf)

/* AST_VIDEO_SCRATCH_350	0x350		Video Scratch Remap Read Back */
#define SCRATCH_VGA_GET_MODE_HEADER(x)			((x >> 8) & 0xff)
#define SCRATCH_VGA_GET_NEW_COLOR_MODE(x)		((x >> 16) & 0xff)
#define SCRATCH_VGA_GET_NEW_PIXEL_CLK(x)		((x >> 24) & 0xff)


/* AST_VIDEO_SCRATCH_35C	0x35C		Video Scratch Remap Read Back */
#define SCRATCH_VGA_PWR_STS_HSYNC				(1 << 31)
#define SCRATCH_VGA_PWR_STS_VSYNC				(1 << 30)
#define SCRATCH_VGA_ATTRIBTE_INDEX_BIT5			(1 << 29)
#define SCRATCH_VGA_MASK_REG					(1 << 28)
#define SCRATCH_VGA_CRT_RST						(1 << 27)
#define SCRATCH_VGA_SCREEN_OFF					(1 << 26)
#define SCRATCH_VGA_RESET						(1 << 25)
#define SCRATCH_VGA_ENABLE						(1 << 24)
/***********************************************************************/
//#define CONFIG_AST_VIDEO_LOCK
#define CONFIG_AUTO_MODE

#define CONFIG_AST_VIDEO_DEBUG

#ifdef CONFIG_AST_VIDEO_DEBUG
#define VIDEO_DBG(fmt, args...) pr_debug("%s() " fmt, __func__, ## args)
#else
#define VIDEO_DBG(fmt, args...)
#endif

//VR08[2]
enum ast_video_source {
	VIDEO_SOURCE_INT_VGA = 0,
	VIDEO_SOURCE_INT_CRT,
	VIDEO_SOURCE_EXT_ADC,
	VIDEO_SOURCE_EXT_DIGITAL,
};

//VR08[5]
enum ast_vga_mode {
	VIDEO_VGA_DIRECT_MODE = 0,
	VIDEO_VGA_CAPTURE_MODE,
};

//VR08[4]
enum ast_video_dis_en {
	VIDEO_EXT_DE_SIGNAL = 0,
	VIDEO_INT_DE_SIGNAL,
};

enum video_color_format {
	VIDEO_COLOR_RGB565 = 0,
	VIDEO_COLOR_RGB888,
	VIDEO_COLOR_YUV444,
	VIDEO_COLOR_YUV420,
};

enum vga_color_mode {
	VGA_NO_SIGNAL = 0,
	EGA_MODE,
	VGA_MODE,
	VGA_15BPP_MODE,
	VGA_16BPP_MODE,
	VGA_32BPP_MODE,
	VGA_CGA_MODE,
	VGA_TEXT_MODE,
};

enum video_stage {
	NONE,
	POLARITY,
	RESOLUTION,
	INIT,
	RUN,
};

struct aspeed_multi_jpeg_frame {
	u32 dwSizeInBytes;			// Image size in bytes
	u32 dwOffsetInBytes;			// Offset in bytes
	u16	wXPixels;				// In: X coordinate
	u16	wYPixels;				// In: Y coordinate
	u16	wWidthPixels;			// In: Width for Fetch
	u16	wHeightPixels;			// In: Height for Fetch
};

#define MAX_MULTI_FRAME_CT (32)

struct aspeed_multi_jpeg_config {
	u8 multi_jpeg_frames;	// frame count
	struct aspeed_multi_jpeg_frame frame[MAX_MULTI_FRAME_CT];	// The Multi Frames
};
/***********************************************************************/
struct ast_video_config {
	u8	engine;					//0: engine 0, engine 1
	u8	compression_mode;		//0:DCT, 1:DCT_VQ mix VQ-2 color, 2:DCT_VQ mix VQ-4 color		9:
	u8	compression_format;		//0:ASPEED 1:JPEG 2:Multi-JPEG
	u8	capture_format;			//0:CCIR601-2 YUV, 1:JPEG YUV, 2:RGB for ASPEED mode only, 3:Gray
	u8	rc4_enable;				//0:disable 1:enable
	u8	EncodeKeys[256];

	u8	YUV420_mode;			//0:YUV444, 1:YUV420
	u8	Visual_Lossless;
	u8	Y_JPEGTableSelector;
	u8	AdvanceTableSelector;
	u8	AutoMode;
};

struct ast_auto_mode {
	u8	engine_idx;					//set 0: engine 0, engine 1
	u8	differential;					//set 0: full, 1:diff frame
	u8	mode_change;				//get 0: no, 1:change
	u32	total_size;					//get
	u32	block_count;					//get
};

struct ast_capture_mode {
	u8	engine_idx;					//set 0: engine 0, engine 1
	u8	differential;					//set 0: full, 1:diff frame
	u8	mode_change;				//get 0: no, 1:change
};

struct ast_compression_mode {
	u8	engine_idx;					//set 0: engine 0, engine 1
	u8	mode_change;				//get 0: no, 1:change
	u32	total_size;					//get
	u32	block_count;					//get
};

struct ast_scaling {
	u8	engine;					//0: engine 0, engine 1
	u8	enable;
	u16	x;
	u16	y;
};

struct ast_mode_detection {
	unsigned char		result;		//0: pass, 1: fail
	unsigned short	src_x;
	unsigned short	src_y;
};

//IOCTL ..
#define VIDEOIOC_BASE       'V'

#define AST_VIDEO_RESET						_IO(VIDEOIOC_BASE, 0x0)
#define AST_VIDEO_IOC_GET_VGA_SIGNAL		_IOR(VIDEOIOC_BASE, 0x1, unsigned char)
#define AST_VIDEO_GET_MEM_SIZE_IOCRX		_IOR(VIDEOIOC_BASE, 0x2, unsigned long)
#define AST_VIDEO_GET_JPEG_OFFSET_IOCRX		_IOR(VIDEOIOC_BASE, 0x3, unsigned long)
#define AST_VIDEO_VGA_MODE_DETECTION		_IOWR(VIDEOIOC_BASE, 0x4, struct ast_mode_detection*)

#define AST_VIDEO_ENG_CONFIG				_IOW(VIDEOIOC_BASE, 0x5, struct ast_video_config*)
#define AST_VIDEO_SET_SCALING				_IOW(VIDEOIOC_BASE, 0x6, struct ast_scaling*)

#define AST_VIDEO_AUTOMODE_TRIGGER			_IOWR(VIDEOIOC_BASE, 0x7, struct ast_auto_mode*)
#define AST_VIDEO_CAPTURE_TRIGGER			_IOWR(VIDEOIOC_BASE, 0x8, struct ast_capture_mode*)
#define AST_VIDEO_COMPRESSION_TRIGGER		_IOWR(VIDEOIOC_BASE, 0x9, struct ast_compression_mode*)

#define AST_VIDEO_SET_VGA_DISPLAY			_IOW(VIDEOIOC_BASE, 0xa, int)
#define AST_VIDEO_SET_ENCRYPTION			_IOW(VIDEOIOC_BASE, 0xb, int)
#define AST_VIDEO_SET_ENCRYPTION_KEY		_IOW(VIDEOIOC_BASE, 0xc, unsigned char*)
#define AST_VIDEO_SET_CRT_COMPRESSION		_IOW(VIDEOIOC_BASE, 0xd, struct fb_var_screeninfo*)

#define AST_VIDEO_MULTIJPEG_AUTOMODE_TRIGGER	_IOWR(VIDEOIOC_BASE, 0xe, struct aspeed_multi_jpeg_config*)
#define AST_VIDEO_MULTIJPEG_TRIGGER			_IOWR(VIDEOIOC_BASE, 0xf, struct aspeed_multi_jpeg_config*)
/***********************************************************************/
struct fbinfo {
	u16		x;
	u16		y;
	u8	color_mode;	//0:NON, 1:EGA, 2:VGA, 3:15bpp, 4:16bpp, 5:32bpp
	u32	PixelClock;
};

//For Socket Transfer head formate ..

struct aspeed_video_config {
	u8		version;
	u32		dram_base;
};

struct aspeed_video_mem {
	dma_addr_t	dma;
	void *virt;
};

struct ast_video_data {
	struct device		*misc_dev;
	void __iomem		*reg_base;			/* virtual */
	struct regmap		*scu;
	struct regmap		*gfx;
	int	irq;				//Video IRQ number
	struct aspeed_video_config	*config;
	struct reset_control *reset;
	struct clk			*vclk;
	struct clk			*eclk;
//	compress_header

	struct aspeed_video_mem		video_mem;

	dma_addr_t             stream_phy;            /* phy */
	void                   *stream_virt;           /* virt */
	dma_addr_t             buff0_phy;             /* phy */
	void                   *buff0_virt;            /* virt */
	dma_addr_t             buff1_phy;             /* phy */
	void                   *buff1_virt;            /* virt */
	dma_addr_t             bcd_phy;               /* phy */
	void                   *bcd_virt;              /* virt */
	dma_addr_t             jpeg_phy;              /* phy */
	void                   *jpeg_virt;             /* virt */
	dma_addr_t             jpeg_buf0_phy;              /* phy */
	void                   *jpeg_buf0_virt;             /* virt */
	dma_addr_t             jpeg_tbl_phy;          /* phy */
	void                   *jpeg_tbl_virt;         /* virt */

	//config
	enum ast_video_source  input_source;
	u8	rc4_enable;
	u8 EncodeKeys[256];
	u8	scaling;

//JPEG
	u32		video_mem_size;			/* phy size*/
	u32		video_jpeg_offset;			/* assigned jpeg memory size*/
	u8 mode_change;
	struct completion	mode_detect_complete;
	struct completion	automode_complete;
	struct completion	capture_complete;
	struct completion	compression_complete;

	wait_queue_head_t	queue;

	u32 flag;
	wait_queue_head_t	video_wq;

	u32 thread_flag;
	struct task_struct		*thread_task;

	struct fbinfo					src_fbinfo;
	struct fbinfo					dest_fbinfo;
	struct completion				complete;
	u32		sts;
	u8		direct_mode;
	u8		stage;
	u32	bandwidth;
	struct mutex lock;

	bool is_open;
	int	multi_jpeg;
};

//  RC4 structure
struct rc4_state {
	int x;
	int y;
	int m[256];
};


/***********************************************************************/
#define AST_SCU_MISC1_CTRL			0x2C		/*	Misc. Control register */
#define SCU_MISC_VGA_CRT_DIS		BIT(6)

static void ast_scu_set_vga_display(struct ast_video_data *ast_video, u8 enable)
{
	if (enable)
		regmap_update_bits(ast_video->scu, AST_SCU_MISC1_CTRL, SCU_MISC_VGA_CRT_DIS, 0);
	else
		regmap_update_bits(ast_video->scu, AST_SCU_MISC1_CTRL, SCU_MISC_VGA_CRT_DIS, SCU_MISC_VGA_CRT_DIS);
}

static int ast_scu_get_vga_display(struct ast_video_data *ast_video)
{
	u32 val;

	regmap_read(ast_video->scu, AST_SCU_MISC1_CTRL, &val);

	if (val & SCU_MISC_VGA_CRT_DIS)
		return 0;
	else
		return 1;
}

/***********************************************************************/


static inline void
ast_video_write(struct ast_video_data *ast_video, u32 val, u32 reg)
{
//	VIDEO_DBG("write offset: %x, val: %x\n",reg,val);
#ifdef CONFIG_AST_VIDEO_LOCK
	//unlock
	writel(VIDEO_PROTECT_UNLOCK, ast_video->reg_base);
	writel(val, ast_video->reg_base + reg);
	//lock
	writel(0xaa, ast_video->reg_base);
#else
	//Video is lock after reset, need always unlock
	//unlock
	writel(VIDEO_PROTECT_UNLOCK, ast_video->reg_base);
	writel(val, ast_video->reg_base + reg);
#endif
}

static inline u32
ast_video_read(struct ast_video_data *ast_video, u32 reg)
{
	u32 val = readl(ast_video->reg_base + reg);
//	VIDEO_DBG("read offset: %x, val: %x\n",reg,val);
	return val;
}

/************************************************ JPEG ***************************************************************************************/
void ast_init_jpeg_table(struct ast_video_data *ast_video)
{
	int i = 0;
	int base = 0;
	u32 *tlb_table = ast_video->jpeg_tbl_virt;
	//JPEG header default value:
	for (i = 0; i < 12; i++) {
		base = (256 * i);
		tlb_table[base + 0] = 0xE0FFD8FF;
		tlb_table[base + 1] = 0x464A1000;
		tlb_table[base + 2] = 0x01004649;
		tlb_table[base + 3] = 0x60000101;
		tlb_table[base + 4] = 0x00006000;
		tlb_table[base + 5] = 0x0F00FEFF;
		tlb_table[base + 6] = 0x00002D05;
		tlb_table[base + 7] = 0x00000000;
		tlb_table[base + 8] = 0x00000000;
		tlb_table[base + 9] = 0x00DBFF00;
		tlb_table[base + 44] = 0x081100C0;
		tlb_table[base + 45] = 0x00000000;
		tlb_table[base + 47] = 0x03011102;
		tlb_table[base + 48] = 0xC4FF0111;
		tlb_table[base + 49] = 0x00001F00;
		tlb_table[base + 50] = 0x01010501;
		tlb_table[base + 51] = 0x01010101;
		tlb_table[base + 52] = 0x00000000;
		tlb_table[base + 53] = 0x00000000;
		tlb_table[base + 54] = 0x04030201;
		tlb_table[base + 55] = 0x08070605;
		tlb_table[base + 56] = 0xFF0B0A09;
		tlb_table[base + 57] = 0x10B500C4;
		tlb_table[base + 58] = 0x03010200;
		tlb_table[base + 59] = 0x03040203;
		tlb_table[base + 60] = 0x04040505;
		tlb_table[base + 61] = 0x7D010000;
		tlb_table[base + 62] = 0x00030201;
		tlb_table[base + 63] = 0x12051104;
		tlb_table[base + 64] = 0x06413121;
		tlb_table[base + 65] = 0x07615113;
		tlb_table[base + 66] = 0x32147122;
		tlb_table[base + 67] = 0x08A19181;
		tlb_table[base + 68] = 0xC1B14223;
		tlb_table[base + 69] = 0xF0D15215;
		tlb_table[base + 70] = 0x72623324;
		tlb_table[base + 71] = 0x160A0982;
		tlb_table[base + 72] = 0x1A191817;
		tlb_table[base + 73] = 0x28272625;
		tlb_table[base + 74] = 0x35342A29;
		tlb_table[base + 75] = 0x39383736;
		tlb_table[base + 76] = 0x4544433A;
		tlb_table[base + 77] = 0x49484746;
		tlb_table[base + 78] = 0x5554534A;
		tlb_table[base + 79] = 0x59585756;
		tlb_table[base + 80] = 0x6564635A;
		tlb_table[base + 81] = 0x69686766;
		tlb_table[base + 82] = 0x7574736A;
		tlb_table[base + 83] = 0x79787776;
		tlb_table[base + 84] = 0x8584837A;
		tlb_table[base + 85] = 0x89888786;
		tlb_table[base + 86] = 0x9493928A;
		tlb_table[base + 87] = 0x98979695;
		tlb_table[base + 88] = 0xA3A29A99;
		tlb_table[base + 89] = 0xA7A6A5A4;
		tlb_table[base + 90] = 0xB2AAA9A8;
		tlb_table[base + 91] = 0xB6B5B4B3;
		tlb_table[base + 92] = 0xBAB9B8B7;
		tlb_table[base + 93] = 0xC5C4C3C2;
		tlb_table[base + 94] = 0xC9C8C7C6;
		tlb_table[base + 95] = 0xD4D3D2CA;
		tlb_table[base + 96] = 0xD8D7D6D5;
		tlb_table[base + 97] = 0xE2E1DAD9;
		tlb_table[base + 98] = 0xE6E5E4E3;
		tlb_table[base + 99] = 0xEAE9E8E7;
		tlb_table[base + 100] = 0xF4F3F2F1;
		tlb_table[base + 101] = 0xF8F7F6F5;
		tlb_table[base + 102] = 0xC4FFFAF9;
		tlb_table[base + 103] = 0x00011F00;
		tlb_table[base + 104] = 0x01010103;
		tlb_table[base + 105] = 0x01010101;
		tlb_table[base + 106] = 0x00000101;
		tlb_table[base + 107] = 0x00000000;
		tlb_table[base + 108] = 0x04030201;
		tlb_table[base + 109] = 0x08070605;
		tlb_table[base + 110] = 0xFF0B0A09;
		tlb_table[base + 111] = 0x11B500C4;
		tlb_table[base + 112] = 0x02010200;
		tlb_table[base + 113] = 0x04030404;
		tlb_table[base + 114] = 0x04040507;
		tlb_table[base + 115] = 0x77020100;
		tlb_table[base + 116] = 0x03020100;
		tlb_table[base + 117] = 0x21050411;
		tlb_table[base + 118] = 0x41120631;
		tlb_table[base + 119] = 0x71610751;
		tlb_table[base + 120] = 0x81322213;
		tlb_table[base + 121] = 0x91421408;
		tlb_table[base + 122] = 0x09C1B1A1;
		tlb_table[base + 123] = 0xF0523323;
		tlb_table[base + 124] = 0xD1726215;
		tlb_table[base + 125] = 0x3424160A;
		tlb_table[base + 126] = 0x17F125E1;
		tlb_table[base + 127] = 0x261A1918;
		tlb_table[base + 128] = 0x2A292827;
		tlb_table[base + 129] = 0x38373635;
		tlb_table[base + 130] = 0x44433A39;
		tlb_table[base + 131] = 0x48474645;
		tlb_table[base + 132] = 0x54534A49;
		tlb_table[base + 133] = 0x58575655;
		tlb_table[base + 134] = 0x64635A59;
		tlb_table[base + 135] = 0x68676665;
		tlb_table[base + 136] = 0x74736A69;
		tlb_table[base + 137] = 0x78777675;
		tlb_table[base + 138] = 0x83827A79;
		tlb_table[base + 139] = 0x87868584;
		tlb_table[base + 140] = 0x928A8988;
		tlb_table[base + 141] = 0x96959493;
		tlb_table[base + 142] = 0x9A999897;
		tlb_table[base + 143] = 0xA5A4A3A2;
		tlb_table[base + 144] = 0xA9A8A7A6;
		tlb_table[base + 145] = 0xB4B3B2AA;
		tlb_table[base + 146] = 0xB8B7B6B5;
		tlb_table[base + 147] = 0xC3C2BAB9;
		tlb_table[base + 148] = 0xC7C6C5C4;
		tlb_table[base + 149] = 0xD2CAC9C8;
		tlb_table[base + 150] = 0xD6D5D4D3;
		tlb_table[base + 151] = 0xDAD9D8D7;
		tlb_table[base + 152] = 0xE5E4E3E2;
		tlb_table[base + 153] = 0xE9E8E7E6;
		tlb_table[base + 154] = 0xF4F3F2EA;
		tlb_table[base + 155] = 0xF8F7F6F5;
		tlb_table[base + 156] = 0xDAFFFAF9;
		tlb_table[base + 157] = 0x01030C00;
		tlb_table[base + 158] = 0x03110200;
		tlb_table[base + 159] = 0x003F0011;

		//Table 0
		if (i == 0) {
			tlb_table[base + 10] = 0x0D140043;
			tlb_table[base + 11] = 0x0C0F110F;
			tlb_table[base + 12] = 0x11101114;
			tlb_table[base + 13] = 0x17141516;
			tlb_table[base + 14] = 0x1E20321E;
			tlb_table[base + 15] = 0x3D1E1B1B;
			tlb_table[base + 16] = 0x32242E2B;
			tlb_table[base + 17] = 0x4B4C3F48;
			tlb_table[base + 18] = 0x44463F47;
			tlb_table[base + 19] = 0x61735A50;
			tlb_table[base + 20] = 0x566C5550;
			tlb_table[base + 21] = 0x88644644;
			tlb_table[base + 22] = 0x7A766C65;
			tlb_table[base + 23] = 0x4D808280;
			tlb_table[base + 24] = 0x8C978D60;
			tlb_table[base + 25] = 0x7E73967D;
			tlb_table[base + 26] = 0xDBFF7B80;
			tlb_table[base + 27] = 0x1F014300;
			tlb_table[base + 28] = 0x272D2121;
			tlb_table[base + 29] = 0x3030582D;
			tlb_table[base + 30] = 0x697BB958;
			tlb_table[base + 31] = 0xB8B9B97B;
			tlb_table[base + 32] = 0xB9B8A6A6;
			tlb_table[base + 33] = 0xB9B9B9B9;
			tlb_table[base + 34] = 0xB9B9B9B9;
			tlb_table[base + 35] = 0xB9B9B9B9;
			tlb_table[base + 36] = 0xB9B9B9B9;
			tlb_table[base + 37] = 0xB9B9B9B9;
			tlb_table[base + 38] = 0xB9B9B9B9;
			tlb_table[base + 39] = 0xB9B9B9B9;
			tlb_table[base + 40] = 0xB9B9B9B9;
			tlb_table[base + 41] = 0xB9B9B9B9;
			tlb_table[base + 42] = 0xB9B9B9B9;
			tlb_table[base + 43] = 0xFFB9B9B9;
		}
		//Table 1
		if (i == 1) {
			tlb_table[base + 10] = 0x0C110043;
			tlb_table[base + 11] = 0x0A0D0F0D;
			tlb_table[base + 12] = 0x0F0E0F11;
			tlb_table[base + 13] = 0x14111213;
			tlb_table[base + 14] = 0x1A1C2B1A;
			tlb_table[base + 15] = 0x351A1818;
			tlb_table[base + 16] = 0x2B1F2826;
			tlb_table[base + 17] = 0x4142373F;
			tlb_table[base + 18] = 0x3C3D373E;
			tlb_table[base + 19] = 0x55644E46;
			tlb_table[base + 20] = 0x4B5F4A46;
			tlb_table[base + 21] = 0x77573D3C;
			tlb_table[base + 22] = 0x6B675F58;
			tlb_table[base + 23] = 0x43707170;
			tlb_table[base + 24] = 0x7A847B54;
			tlb_table[base + 25] = 0x6E64836D;
			tlb_table[base + 26] = 0xDBFF6C70;
			tlb_table[base + 27] = 0x1B014300;
			tlb_table[base + 28] = 0x22271D1D;
			tlb_table[base + 29] = 0x2A2A4C27;
			tlb_table[base + 30] = 0x5B6BA04C;
			tlb_table[base + 31] = 0xA0A0A06B;
			tlb_table[base + 32] = 0xA0A0A0A0;
			tlb_table[base + 33] = 0xA0A0A0A0;
			tlb_table[base + 34] = 0xA0A0A0A0;
			tlb_table[base + 35] = 0xA0A0A0A0;
			tlb_table[base + 36] = 0xA0A0A0A0;
			tlb_table[base + 37] = 0xA0A0A0A0;
			tlb_table[base + 38] = 0xA0A0A0A0;
			tlb_table[base + 39] = 0xA0A0A0A0;
			tlb_table[base + 40] = 0xA0A0A0A0;
			tlb_table[base + 41] = 0xA0A0A0A0;
			tlb_table[base + 42] = 0xA0A0A0A0;
			tlb_table[base + 43] = 0xFFA0A0A0;
		}
		//Table 2
		if (i == 2) {
			tlb_table[base + 10] = 0x090E0043;
			tlb_table[base + 11] = 0x090A0C0A;
			tlb_table[base + 12] = 0x0C0B0C0E;
			tlb_table[base + 13] = 0x110E0F10;
			tlb_table[base + 14] = 0x15172415;
			tlb_table[base + 15] = 0x2C151313;
			tlb_table[base + 16] = 0x241A211F;
			tlb_table[base + 17] = 0x36372E34;
			tlb_table[base + 18] = 0x31322E33;
			tlb_table[base + 19] = 0x4653413A;
			tlb_table[base + 20] = 0x3E4E3D3A;
			tlb_table[base + 21] = 0x62483231;
			tlb_table[base + 22] = 0x58564E49;
			tlb_table[base + 23] = 0x385D5E5D;
			tlb_table[base + 24] = 0x656D6645;
			tlb_table[base + 25] = 0x5B536C5A;
			tlb_table[base + 26] = 0xDBFF595D;
			tlb_table[base + 27] = 0x16014300;
			tlb_table[base + 28] = 0x1C201818;
			tlb_table[base + 29] = 0x22223F20;
			tlb_table[base + 30] = 0x4B58853F;
			tlb_table[base + 31] = 0x85858558;
			tlb_table[base + 32] = 0x85858585;
			tlb_table[base + 33] = 0x85858585;
			tlb_table[base + 34] = 0x85858585;
			tlb_table[base + 35] = 0x85858585;
			tlb_table[base + 36] = 0x85858585;
			tlb_table[base + 37] = 0x85858585;
			tlb_table[base + 38] = 0x85858585;
			tlb_table[base + 39] = 0x85858585;
			tlb_table[base + 40] = 0x85858585;
			tlb_table[base + 41] = 0x85858585;
			tlb_table[base + 42] = 0x85858585;
			tlb_table[base + 43] = 0xFF858585;
		}
		//Table 3
		if (i == 3) {
			tlb_table[base + 10] = 0x070B0043;
			tlb_table[base + 11] = 0x07080A08;
			tlb_table[base + 12] = 0x0A090A0B;
			tlb_table[base + 13] = 0x0D0B0C0C;
			tlb_table[base + 14] = 0x11121C11;
			tlb_table[base + 15] = 0x23110F0F;
			tlb_table[base + 16] = 0x1C141A19;
			tlb_table[base + 17] = 0x2B2B2429;
			tlb_table[base + 18] = 0x27282428;
			tlb_table[base + 19] = 0x3842332E;
			tlb_table[base + 20] = 0x313E302E;
			tlb_table[base + 21] = 0x4E392827;
			tlb_table[base + 22] = 0x46443E3A;
			tlb_table[base + 23] = 0x2C4A4A4A;
			tlb_table[base + 24] = 0x50565137;
			tlb_table[base + 25] = 0x48425647;
			tlb_table[base + 26] = 0xDBFF474A;
			tlb_table[base + 27] = 0x12014300;
			tlb_table[base + 28] = 0x161A1313;
			tlb_table[base + 29] = 0x1C1C331A;
			tlb_table[base + 30] = 0x3D486C33;
			tlb_table[base + 31] = 0x6C6C6C48;
			tlb_table[base + 32] = 0x6C6C6C6C;
			tlb_table[base + 33] = 0x6C6C6C6C;
			tlb_table[base + 34] = 0x6C6C6C6C;
			tlb_table[base + 35] = 0x6C6C6C6C;
			tlb_table[base + 36] = 0x6C6C6C6C;
			tlb_table[base + 37] = 0x6C6C6C6C;
			tlb_table[base + 38] = 0x6C6C6C6C;
			tlb_table[base + 39] = 0x6C6C6C6C;
			tlb_table[base + 40] = 0x6C6C6C6C;
			tlb_table[base + 41] = 0x6C6C6C6C;
			tlb_table[base + 42] = 0x6C6C6C6C;
			tlb_table[base + 43] = 0xFF6C6C6C;
		}
		//Table 4
		if (i == 4) {
			tlb_table[base + 10] = 0x06090043;
			tlb_table[base + 11] = 0x05060706;
			tlb_table[base + 12] = 0x07070709;
			tlb_table[base + 13] = 0x0A09090A;
			tlb_table[base + 14] = 0x0D0E160D;
			tlb_table[base + 15] = 0x1B0D0C0C;
			tlb_table[base + 16] = 0x16101413;
			tlb_table[base + 17] = 0x21221C20;
			tlb_table[base + 18] = 0x1E1F1C20;
			tlb_table[base + 19] = 0x2B332824;
			tlb_table[base + 20] = 0x26302624;
			tlb_table[base + 21] = 0x3D2D1F1E;
			tlb_table[base + 22] = 0x3735302D;
			tlb_table[base + 23] = 0x22393A39;
			tlb_table[base + 24] = 0x3F443F2B;
			tlb_table[base + 25] = 0x38334338;
			tlb_table[base + 26] = 0xDBFF3739;
			tlb_table[base + 27] = 0x0D014300;
			tlb_table[base + 28] = 0x11130E0E;
			tlb_table[base + 29] = 0x15152613;
			tlb_table[base + 30] = 0x2D355026;
			tlb_table[base + 31] = 0x50505035;
			tlb_table[base + 32] = 0x50505050;
			tlb_table[base + 33] = 0x50505050;
			tlb_table[base + 34] = 0x50505050;
			tlb_table[base + 35] = 0x50505050;
			tlb_table[base + 36] = 0x50505050;
			tlb_table[base + 37] = 0x50505050;
			tlb_table[base + 38] = 0x50505050;
			tlb_table[base + 39] = 0x50505050;
			tlb_table[base + 40] = 0x50505050;
			tlb_table[base + 41] = 0x50505050;
			tlb_table[base + 42] = 0x50505050;
			tlb_table[base + 43] = 0xFF505050;
		}
		//Table 5
		if (i == 5) {
			tlb_table[base + 10] = 0x04060043;
			tlb_table[base + 11] = 0x03040504;
			tlb_table[base + 12] = 0x05040506;
			tlb_table[base + 13] = 0x07060606;
			tlb_table[base + 14] = 0x09090F09;
			tlb_table[base + 15] = 0x12090808;
			tlb_table[base + 16] = 0x0F0A0D0D;
			tlb_table[base + 17] = 0x16161315;
			tlb_table[base + 18] = 0x14151315;
			tlb_table[base + 19] = 0x1D221B18;
			tlb_table[base + 20] = 0x19201918;
			tlb_table[base + 21] = 0x281E1514;
			tlb_table[base + 22] = 0x2423201E;
			tlb_table[base + 23] = 0x17262726;
			tlb_table[base + 24] = 0x2A2D2A1C;
			tlb_table[base + 25] = 0x25222D25;
			tlb_table[base + 26] = 0xDBFF2526;
			tlb_table[base + 27] = 0x09014300;
			tlb_table[base + 28] = 0x0B0D0A0A;
			tlb_table[base + 29] = 0x0E0E1A0D;
			tlb_table[base + 30] = 0x1F25371A;
			tlb_table[base + 31] = 0x37373725;
			tlb_table[base + 32] = 0x37373737;
			tlb_table[base + 33] = 0x37373737;
			tlb_table[base + 34] = 0x37373737;
			tlb_table[base + 35] = 0x37373737;
			tlb_table[base + 36] = 0x37373737;
			tlb_table[base + 37] = 0x37373737;
			tlb_table[base + 38] = 0x37373737;
			tlb_table[base + 39] = 0x37373737;
			tlb_table[base + 40] = 0x37373737;
			tlb_table[base + 41] = 0x37373737;
			tlb_table[base + 42] = 0x37373737;
			tlb_table[base + 43] = 0xFF373737;
		}
		//Table 6
		if (i == 6) {
			tlb_table[base + 10] = 0x02030043;
			tlb_table[base + 11] = 0x01020202;
			tlb_table[base + 12] = 0x02020203;
			tlb_table[base + 13] = 0x03030303;
			tlb_table[base + 14] = 0x04040704;
			tlb_table[base + 15] = 0x09040404;
			tlb_table[base + 16] = 0x07050606;
			tlb_table[base + 17] = 0x0B0B090A;
			tlb_table[base + 18] = 0x0A0A090A;
			tlb_table[base + 19] = 0x0E110D0C;
			tlb_table[base + 20] = 0x0C100C0C;
			tlb_table[base + 21] = 0x140F0A0A;
			tlb_table[base + 22] = 0x1211100F;
			tlb_table[base + 23] = 0x0B131313;
			tlb_table[base + 24] = 0x1516150E;
			tlb_table[base + 25] = 0x12111612;
			tlb_table[base + 26] = 0xDBFF1213;
			tlb_table[base + 27] = 0x04014300;
			tlb_table[base + 28] = 0x05060505;
			tlb_table[base + 29] = 0x07070D06;
			tlb_table[base + 30] = 0x0F121B0D;
			tlb_table[base + 31] = 0x1B1B1B12;
			tlb_table[base + 32] = 0x1B1B1B1B;
			tlb_table[base + 33] = 0x1B1B1B1B;
			tlb_table[base + 34] = 0x1B1B1B1B;
			tlb_table[base + 35] = 0x1B1B1B1B;
			tlb_table[base + 36] = 0x1B1B1B1B;
			tlb_table[base + 37] = 0x1B1B1B1B;
			tlb_table[base + 38] = 0x1B1B1B1B;
			tlb_table[base + 39] = 0x1B1B1B1B;
			tlb_table[base + 40] = 0x1B1B1B1B;
			tlb_table[base + 41] = 0x1B1B1B1B;
			tlb_table[base + 42] = 0x1B1B1B1B;
			tlb_table[base + 43] = 0xFF1B1B1B;
		}
		//Table 7
		if (i == 7) {
			tlb_table[base + 10] = 0x01020043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010102;
			tlb_table[base + 13] = 0x02020202;
			tlb_table[base + 14] = 0x03030503;
			tlb_table[base + 15] = 0x06030202;
			tlb_table[base + 16] = 0x05030404;
			tlb_table[base + 17] = 0x07070607;
			tlb_table[base + 18] = 0x06070607;
			tlb_table[base + 19] = 0x090B0908;
			tlb_table[base + 20] = 0x080A0808;
			tlb_table[base + 21] = 0x0D0A0706;
			tlb_table[base + 22] = 0x0C0B0A0A;
			tlb_table[base + 23] = 0x070C0D0C;
			tlb_table[base + 24] = 0x0E0F0E09;
			tlb_table[base + 25] = 0x0C0B0F0C;
			tlb_table[base + 26] = 0xDBFF0C0C;
			tlb_table[base + 27] = 0x03014300;
			tlb_table[base + 28] = 0x03040303;
			tlb_table[base + 29] = 0x04040804;
			tlb_table[base + 30] = 0x0A0C1208;
			tlb_table[base + 31] = 0x1212120C;
			tlb_table[base + 32] = 0x12121212;
			tlb_table[base + 33] = 0x12121212;
			tlb_table[base + 34] = 0x12121212;
			tlb_table[base + 35] = 0x12121212;
			tlb_table[base + 36] = 0x12121212;
			tlb_table[base + 37] = 0x12121212;
			tlb_table[base + 38] = 0x12121212;
			tlb_table[base + 39] = 0x12121212;
			tlb_table[base + 40] = 0x12121212;
			tlb_table[base + 41] = 0x12121212;
			tlb_table[base + 42] = 0x12121212;
			tlb_table[base + 43] = 0xFF121212;
		}
		//Table 8
		if (i == 8) {
			tlb_table[base + 10] = 0x01020043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010102;
			tlb_table[base + 13] = 0x02020202;
			tlb_table[base + 14] = 0x03030503;
			tlb_table[base + 15] = 0x06030202;
			tlb_table[base + 16] = 0x05030404;
			tlb_table[base + 17] = 0x07070607;
			tlb_table[base + 18] = 0x06070607;
			tlb_table[base + 19] = 0x090B0908;
			tlb_table[base + 20] = 0x080A0808;
			tlb_table[base + 21] = 0x0D0A0706;
			tlb_table[base + 22] = 0x0C0B0A0A;
			tlb_table[base + 23] = 0x070C0D0C;
			tlb_table[base + 24] = 0x0E0F0E09;
			tlb_table[base + 25] = 0x0C0B0F0C;
			tlb_table[base + 26] = 0xDBFF0C0C;
			tlb_table[base + 27] = 0x02014300;
			tlb_table[base + 28] = 0x03030202;
			tlb_table[base + 29] = 0x04040703;
			tlb_table[base + 30] = 0x080A0F07;
			tlb_table[base + 31] = 0x0F0F0F0A;
			tlb_table[base + 32] = 0x0F0F0F0F;
			tlb_table[base + 33] = 0x0F0F0F0F;
			tlb_table[base + 34] = 0x0F0F0F0F;
			tlb_table[base + 35] = 0x0F0F0F0F;
			tlb_table[base + 36] = 0x0F0F0F0F;
			tlb_table[base + 37] = 0x0F0F0F0F;
			tlb_table[base + 38] = 0x0F0F0F0F;
			tlb_table[base + 39] = 0x0F0F0F0F;
			tlb_table[base + 40] = 0x0F0F0F0F;
			tlb_table[base + 41] = 0x0F0F0F0F;
			tlb_table[base + 42] = 0x0F0F0F0F;
			tlb_table[base + 43] = 0xFF0F0F0F;
		}
		//Table 9
		if (i == 9) {
			tlb_table[base + 10] = 0x01010043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010101;
			tlb_table[base + 13] = 0x01010101;
			tlb_table[base + 14] = 0x02020302;
			tlb_table[base + 15] = 0x04020202;
			tlb_table[base + 16] = 0x03020303;
			tlb_table[base + 17] = 0x05050405;
			tlb_table[base + 18] = 0x05050405;
			tlb_table[base + 19] = 0x07080606;
			tlb_table[base + 20] = 0x06080606;
			tlb_table[base + 21] = 0x0A070505;
			tlb_table[base + 22] = 0x09080807;
			tlb_table[base + 23] = 0x05090909;
			tlb_table[base + 24] = 0x0A0B0A07;
			tlb_table[base + 25] = 0x09080B09;
			tlb_table[base + 26] = 0xDBFF0909;
			tlb_table[base + 27] = 0x02014300;
			tlb_table[base + 28] = 0x02030202;
			tlb_table[base + 29] = 0x03030503;
			tlb_table[base + 30] = 0x07080C05;
			tlb_table[base + 31] = 0x0C0C0C08;
			tlb_table[base + 32] = 0x0C0C0C0C;
			tlb_table[base + 33] = 0x0C0C0C0C;
			tlb_table[base + 34] = 0x0C0C0C0C;
			tlb_table[base + 35] = 0x0C0C0C0C;
			tlb_table[base + 36] = 0x0C0C0C0C;
			tlb_table[base + 37] = 0x0C0C0C0C;
			tlb_table[base + 38] = 0x0C0C0C0C;
			tlb_table[base + 39] = 0x0C0C0C0C;
			tlb_table[base + 40] = 0x0C0C0C0C;
			tlb_table[base + 41] = 0x0C0C0C0C;
			tlb_table[base + 42] = 0x0C0C0C0C;
			tlb_table[base + 43] = 0xFF0C0C0C;
		}
		//Table 10
		if (i == 10) {
			tlb_table[base + 10] = 0x01010043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010101;
			tlb_table[base + 13] = 0x01010101;
			tlb_table[base + 14] = 0x01010201;
			tlb_table[base + 15] = 0x03010101;
			tlb_table[base + 16] = 0x02010202;
			tlb_table[base + 17] = 0x03030303;
			tlb_table[base + 18] = 0x03030303;
			tlb_table[base + 19] = 0x04050404;
			tlb_table[base + 20] = 0x04050404;
			tlb_table[base + 21] = 0x06050303;
			tlb_table[base + 22] = 0x06050505;
			tlb_table[base + 23] = 0x03060606;
			tlb_table[base + 24] = 0x07070704;
			tlb_table[base + 25] = 0x06050706;
			tlb_table[base + 26] = 0xDBFF0606;
			tlb_table[base + 27] = 0x01014300;
			tlb_table[base + 28] = 0x01020101;
			tlb_table[base + 29] = 0x02020402;
			tlb_table[base + 30] = 0x05060904;
			tlb_table[base + 31] = 0x09090906;
			tlb_table[base + 32] = 0x09090909;
			tlb_table[base + 33] = 0x09090909;
			tlb_table[base + 34] = 0x09090909;
			tlb_table[base + 35] = 0x09090909;
			tlb_table[base + 36] = 0x09090909;
			tlb_table[base + 37] = 0x09090909;
			tlb_table[base + 38] = 0x09090909;
			tlb_table[base + 39] = 0x09090909;
			tlb_table[base + 40] = 0x09090909;
			tlb_table[base + 41] = 0x09090909;
			tlb_table[base + 42] = 0x09090909;
			tlb_table[base + 43] = 0xFF090909;
		}
		//Table 11
		if (i == 11) {
			tlb_table[base + 10] = 0x01010043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010101;
			tlb_table[base + 13] = 0x01010101;
			tlb_table[base + 14] = 0x01010101;
			tlb_table[base + 15] = 0x01010101;
			tlb_table[base + 16] = 0x01010101;
			tlb_table[base + 17] = 0x01010101;
			tlb_table[base + 18] = 0x01010101;
			tlb_table[base + 19] = 0x02020202;
			tlb_table[base + 20] = 0x02020202;
			tlb_table[base + 21] = 0x03020101;
			tlb_table[base + 22] = 0x03020202;
			tlb_table[base + 23] = 0x01030303;
			tlb_table[base + 24] = 0x03030302;
			tlb_table[base + 25] = 0x03020303;
			tlb_table[base + 26] = 0xDBFF0403;
			tlb_table[base + 27] = 0x01014300;
			tlb_table[base + 28] = 0x01010101;
			tlb_table[base + 29] = 0x01010201;
			tlb_table[base + 30] = 0x03040602;
			tlb_table[base + 31] = 0x06060604;
			tlb_table[base + 32] = 0x06060606;
			tlb_table[base + 33] = 0x06060606;
			tlb_table[base + 34] = 0x06060606;
			tlb_table[base + 35] = 0x06060606;
			tlb_table[base + 36] = 0x06060606;
			tlb_table[base + 37] = 0x06060606;
			tlb_table[base + 38] = 0x06060606;
			tlb_table[base + 39] = 0x06060606;
			tlb_table[base + 40] = 0x06060606;
			tlb_table[base + 41] = 0x06060606;
			tlb_table[base + 42] = 0x06060606;
			tlb_table[base + 43] = 0xFF060606;
		}
	}


}

static void ast_video_encryption_key_setup(struct ast_video_data *ast_video)
{
	int i, j, k, a, StringLength;
	struct rc4_state *s = kmalloc(sizeof(struct rc4_state), GFP_KERNEL);
	u8 *expkey = kmalloc(256, GFP_KERNEL);
	u32     temp;

	if (!s || !expkey)
		goto out_free;
	//key expansion
	StringLength = strlen(ast_video->EncodeKeys);
//	pr_info("key %s , len = %d\n",ast_video->EncodeKeys, StringLength);
	for (i = 0; i < 256; i++) {
		expkey[i] = ast_video->EncodeKeys[i % StringLength];
//		pr_info(" %x ", expkey[i]);
	}
//	pr_info("\n");
	//rc4 setup
	s->x = 0;
	s->y = 0;

	for (i = 0; i < 256; i++)
		s->m[i] = i;

	j = k = 0;
	for (i = 0; i < 256; i++) {
		a = s->m[i];
		j = (unsigned char)(j + a + expkey[k]);
		s->m[i] = s->m[j];
		s->m[j] = a;
		k++;
	}
	for (i = 0; i < 64; i++) {
		temp = s->m[i * 4] + ((s->m[i * 4 + 1]) << 8) + ((s->m[i * 4 + 2]) << 16) + ((s->m[i * 4 + 3]) << 24);
		ast_video_write(ast_video, temp, AST_VIDEO_ENCRYPT_SRAM + i * 4);
	}
out_free:
	kfree(s);
	kfree(expkey);
}

static u8 ast_get_vga_signal(struct ast_video_data *ast_video)
{
	u32 VR34C, VR350, VR35C;
	u8	color_mode;

	VR35C = ast_video_read(ast_video, AST_VIDEO_SCRATCH_35C);
	VR35C &= 0xff000000;

	if (VR35C & (SCRATCH_VGA_PWR_STS_HSYNC | SCRATCH_VGA_PWR_STS_VSYNC)) {
		VIDEO_DBG("No VGA Signal : PWR STS %x\n", VR35C);
		return VGA_NO_SIGNAL;
	}
	if (VR35C == SCRATCH_VGA_MASK_REG) {
		VIDEO_DBG("No VGA Signal : MASK %x\n", VR35C);
		return VGA_NO_SIGNAL;
	}
	if (VR35C & SCRATCH_VGA_SCREEN_OFF) {
		VIDEO_DBG("No VGA Signal : Screen off %x\n", VR35C);
		return VGA_NO_SIGNAL;
	}
	if (!(VR35C & (SCRATCH_VGA_ATTRIBTE_INDEX_BIT5 | SCRATCH_VGA_MASK_REG | SCRATCH_VGA_CRT_RST | SCRATCH_VGA_RESET | SCRATCH_VGA_ENABLE))) {
		VIDEO_DBG("NO VGA Signal : unknown %x\n", VR35C);
		return VGA_NO_SIGNAL;
	}

	VIDEO_DBG("VGA Signal VR35C %x\n", VR35C);
	VR350 = ast_video_read(ast_video, AST_VIDEO_SCRATCH_350);
	if (SCRATCH_VGA_GET_MODE_HEADER(VR350) == 0xA8) {
		color_mode = SCRATCH_VGA_GET_NEW_COLOR_MODE(VR350);
	} else {
		VR34C = ast_video_read(ast_video, AST_VIDEO_SCRATCH_34C);
		if (SCRATCH_VGA_GET_COLOR_MODE(VR34C) >= VGA_15BPP_MODE)
			color_mode = SCRATCH_VGA_GET_COLOR_MODE(VR34C);
		else
			color_mode = SCRATCH_VGA_GET_COLOR_MODE(VR34C);
	}

	if (color_mode == 0) {
		VIDEO_DBG("EGA Mode\n");
		ast_video->src_fbinfo.color_mode = EGA_MODE;
		return EGA_MODE;
	}
	if (color_mode == 1) {
		VIDEO_DBG("VGA Mode\n");
		ast_video->src_fbinfo.color_mode = VGA_MODE;
		return VGA_MODE;
	}
	if (color_mode == 2) {
		VIDEO_DBG("15BPP Mode\n");
		ast_video->src_fbinfo.color_mode = VGA_15BPP_MODE;
		return VGA_15BPP_MODE;
	}
	if (color_mode == 3) {
		VIDEO_DBG("16BPP Mode\n");
		ast_video->src_fbinfo.color_mode = VGA_16BPP_MODE;
		return VGA_16BPP_MODE;
	}
	if (color_mode == 4) {
		VIDEO_DBG("32BPP Mode\n");
		ast_video->src_fbinfo.color_mode = VGA_32BPP_MODE;
		return VGA_32BPP_MODE;
	}
	pr_info("TODO ... unknown ..\n");
	ast_video->src_fbinfo.color_mode = VGA_MODE;
	return VGA_MODE;
}

static void ast_video_set_eng_config(struct ast_video_data *ast_video, struct ast_video_config *video_config)
{
	int i, base = 0;
	u32 ctrl = 0;	//for VR004, VR204
	u32 compress_ctrl = 0x00080000;
	u32 *tlb_table = ast_video->jpeg_tbl_virt;

	VIDEO_DBG("\n");

	switch (video_config->engine) {
	case 0:
		ctrl = ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL);
		break;
	case 1:
		ctrl = ast_video_read(ast_video, AST_VM_SEQ_CTRL);
		break;
	}

	if (video_config->AutoMode)
		ctrl |= VIDEO_AUTO_COMPRESS;
	else
		ctrl &= ~VIDEO_AUTO_COMPRESS;

	ast_video_write(ast_video, VIDEO_COMPRESS_COMPLETE | VIDEO_CAPTURE_COMPLETE | VIDEO_MODE_DETECT_WDT, AST_VIDEO_INT_EN);

	if (ast_video->config->version >= 6) {
		switch (video_config->compression_format) {
		case 2:
			ast_video->multi_jpeg = 1;
			ctrl &= ~G5_VIDEO_COMPRESS_JPEG_MODE;
			ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) | G6_VIDEO_MULTI_JPEG_FLAG_MODE) &
					~(G6_VIDEO_FRAME_CT_MASK | G6_VIDEO_MULTI_JPEG_MODE), AST_VIDEO_PASS_CTRL);
			break;
		case 0:
			ctrl &= ~G5_VIDEO_COMPRESS_JPEG_MODE;
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) &
					~(G6_VIDEO_FRAME_CT_MASK | G6_VIDEO_MULTI_JPEG_MODE | G6_VIDEO_MULTI_JPEG_FLAG_MODE), AST_VIDEO_PASS_CTRL);
			break;
		case 1:
			ctrl |= G5_VIDEO_COMPRESS_JPEG_MODE;
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) &
					~(G6_VIDEO_FRAME_CT_MASK | G6_VIDEO_MULTI_JPEG_MODE | G6_VIDEO_MULTI_JPEG_FLAG_MODE), AST_VIDEO_PASS_CTRL);
			break;
		}
	} else if (ast_video->config->version == 5) {
		if (video_config->compression_format)
			ctrl |= G5_VIDEO_COMPRESS_JPEG_MODE;
		else
			ctrl &= ~G5_VIDEO_COMPRESS_JPEG_MODE;
	} else {
		if (video_config->compression_format)
			ctrl |= VIDEO_COMPRESS_JPEG_MODE;
		else
			ctrl &= ~VIDEO_COMPRESS_JPEG_MODE;
	}
	ctrl &= ~VIDEO_COMPRESS_FORMAT_MASK;

	if (video_config->YUV420_mode)
		ctrl |= VIDEO_COMPRESS_FORMAT(YUV420);

	if (video_config->rc4_enable)
		compress_ctrl |= VIDEO_ENCRYP_ENABLE;

	switch (video_config->compression_mode) {
	case 0:	//DCT only
		compress_ctrl |= VIDEO_DCT_ONLY_ENCODE;
		break;
	case 1:	//DCT VQ mix 2-color
		compress_ctrl &= ~(VIDEO_4COLOR_VQ_ENCODE | VIDEO_DCT_ONLY_ENCODE);
		break;
	case 2:	//DCT VQ mix 4-color
		compress_ctrl |= VIDEO_4COLOR_VQ_ENCODE;
		break;
	default:
		pr_info("error for compression mode~~~~\n");
		break;
	}

	if (video_config->Visual_Lossless) {
		compress_ctrl |= VIDEO_HQ_ENABLE;
		compress_ctrl |= VIDEO_HQ_DCT_LUM(video_config->AdvanceTableSelector);
		compress_ctrl |= VIDEO_HQ_DCT_CHROM((video_config->AdvanceTableSelector + 16));
	} else
		compress_ctrl &= ~VIDEO_HQ_ENABLE;

	switch (video_config->engine) {
	case 0:
		ast_video_write(ast_video, ctrl, AST_VIDEO_SEQ_CTRL);
		ast_video_write(ast_video, compress_ctrl | VIDEO_DCT_LUM(video_config->Y_JPEGTableSelector) | VIDEO_DCT_CHROM(video_config->Y_JPEGTableSelector + 16), AST_VIDEO_COMPRESS_CTRL);
		break;
	case 1:
		ast_video_write(ast_video, ctrl, AST_VM_SEQ_CTRL);
		ast_video_write(ast_video, compress_ctrl | VIDEO_DCT_LUM(video_config->Y_JPEGTableSelector) | VIDEO_DCT_CHROM(video_config->Y_JPEGTableSelector + 16), AST_VM_COMPRESS_CTRL);
		break;
	}

	if (video_config->compression_format >= 1) {
		for (i = 0; i < 12; i++) {
			base = (1024 * i);
			if (video_config->YUV420_mode)	//yuv420
				tlb_table[base + 46] = 0x00220103; //for YUV420 mode
			else
				tlb_table[base + 46] = 0x00110103; //for YUV444 mode)
		}
	}
}

static void ast_video_set_0_scaling(struct ast_video_data *ast_video, struct ast_scaling *scaling)
{
	u32 scan_line, v_factor, h_factor;
	u32 ctrl = ast_video_read(ast_video, AST_VIDEO_CTRL);
	//no scaling
	ctrl &= ~VIDEO_CTRL_DWN_SCALING_MASK;

	if (scaling->enable) {
		if ((ast_video->src_fbinfo.x == scaling->x) && (ast_video->src_fbinfo.y == scaling->y)) {
			ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING0);
			ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING1);
			ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING2);
			ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING3);
			//compression x,y
			ast_video_write(ast_video, VIDEO_COMPRESS_H(ast_video->src_fbinfo.x) | VIDEO_COMPRESS_V(ast_video->src_fbinfo.y), AST_VIDEO_COMPRESS_WIN);
			ast_video_write(ast_video, 0x10001000, AST_VIDEO_SCAL_FACTOR);
		} else {
			//Down-Scaling
			VIDEO_DBG("Scaling Enable\n");
			//Calculate scaling factor D / S = 4096 / Factor  ======> Factor = (S / D) * 4096
			h_factor = ((ast_video->src_fbinfo.x - 1) * 4096) / (scaling->x - 1);
			if (h_factor < 4096)
				h_factor = 4096;
			if ((h_factor * (scaling->x - 1)) != (ast_video->src_fbinfo.x - 1) * 4096)
				h_factor += 1;

			//Calculate scaling factor D / S = 4096 / Factor	======> Factor = (S / D) * 4096
			v_factor = ((ast_video->src_fbinfo.y - 1) * 4096) / (scaling->y - 1);
			if (v_factor < 4096)
				v_factor = 4096;
			if ((v_factor * (scaling->y - 1)) != (ast_video->src_fbinfo.y - 1) * 4096)
				v_factor += 1;

			if ((ast_video->config->version != 5) && (ast_video->config->version != 6))
				ctrl |= VIDEO_CTRL_DWN_SCALING_ENABLE_LINE_BUFFER;

			if (ast_video->src_fbinfo.x <= scaling->x * 2) {
				ast_video_write(ast_video, 0x00101000, AST_VIDEO_SCALING0);
				ast_video_write(ast_video, 0x00101000, AST_VIDEO_SCALING1);
				ast_video_write(ast_video, 0x00101000, AST_VIDEO_SCALING2);
				ast_video_write(ast_video, 0x00101000, AST_VIDEO_SCALING3);
			} else {
				ast_video_write(ast_video, 0x08080808, AST_VIDEO_SCALING0);
				ast_video_write(ast_video, 0x08080808, AST_VIDEO_SCALING1);
				ast_video_write(ast_video, 0x08080808, AST_VIDEO_SCALING2);
				ast_video_write(ast_video, 0x08080808, AST_VIDEO_SCALING3);
			}
			//compression x,y
			ast_video_write(ast_video, VIDEO_COMPRESS_H(scaling->x) | VIDEO_COMPRESS_V(scaling->y), AST_VIDEO_COMPRESS_WIN);

			VIDEO_DBG("Scaling factor : v : %d , h : %d\n", v_factor, h_factor);
			ast_video_write(ast_video, VIDEO_V_SCAL_FACTOR(v_factor) | VIDEO_H_SCAL_FACTOR(h_factor), AST_VIDEO_SCAL_FACTOR);
		}
	} else {// 1:1
		VIDEO_DBG("Scaling Disable\n");
		v_factor = 4096;
		h_factor = 4096;
		ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING0);
		ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING1);
		ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING2);
		ast_video_write(ast_video, 0x00200000, AST_VIDEO_SCALING3);
		//compression x,y
		ast_video_write(ast_video, VIDEO_COMPRESS_H(ast_video->src_fbinfo.x) | VIDEO_COMPRESS_V(ast_video->src_fbinfo.y), AST_VIDEO_COMPRESS_WIN);

		ast_video_write(ast_video, 0x10001000, AST_VIDEO_SCAL_FACTOR);
	}
	ast_video_write(ast_video, ctrl, AST_VIDEO_CTRL);

	//capture x y
	if ((ast_video->config->version == 5) || (ast_video->config->version == 6)) {
		//A1 issue fix
		if (ast_video->src_fbinfo.x == 1680)
			ast_video_write(ast_video, VIDEO_CAPTURE_H(1728) | VIDEO_CAPTURE_V(ast_video->src_fbinfo.y), AST_VIDEO_CAPTURE_WIN);
		else
			ast_video_write(ast_video, VIDEO_CAPTURE_H(ast_video->src_fbinfo.x) |	VIDEO_CAPTURE_V(ast_video->src_fbinfo.y), AST_VIDEO_CAPTURE_WIN);
	} else {
		ast_video_write(ast_video, VIDEO_CAPTURE_H(ast_video->src_fbinfo.x) |	VIDEO_CAPTURE_V(ast_video->src_fbinfo.y), AST_VIDEO_CAPTURE_WIN);
	}


	if ((ast_video->src_fbinfo.x % 8) == 0)
		ast_video_write(ast_video, ast_video->src_fbinfo.x * 4, AST_VIDEO_SOURCE_SCAN_LINE);
	else {
		scan_line = ast_video->src_fbinfo.x;
		scan_line = scan_line + 16 - (scan_line % 16);
		scan_line = scan_line * 4;
		ast_video_write(ast_video, scan_line, AST_VIDEO_SOURCE_SCAN_LINE);
	}

}

static void ast_video_set_1_scaling(struct ast_video_data *ast_video, struct ast_scaling *scaling)
{
	u32 v_factor, h_factor;

	if (scaling->enable) {
		if ((ast_video->src_fbinfo.x == scaling->x) && (ast_video->src_fbinfo.y == scaling->y)) {
			ast_video_write(ast_video, VIDEO_COMPRESS_H(ast_video->src_fbinfo.x) | VIDEO_COMPRESS_V(ast_video->src_fbinfo.y), AST_VM_COMPRESS_WIN);
			ast_video_write(ast_video, 0x10001000, AST_VM_SCAL_FACTOR);
		} else {
			//Down-Scaling
			VIDEO_DBG("Scaling Enable\n");
			//Calculate scaling factor D / S = 4096 / Factor  ======> Factor = (S / D) * 4096
			h_factor = ((ast_video->src_fbinfo.x - 1) * 4096) / (scaling->x - 1);
			if (h_factor < 4096)
				h_factor = 4096;
			if ((h_factor * (scaling->x - 1)) != (ast_video->src_fbinfo.x - 1) * 4096)
				h_factor += 1;

			//Calculate scaling factor D / S = 4096 / Factor	======> Factor = (S / D) * 4096
			v_factor = ((ast_video->src_fbinfo.y - 1) * 4096) / (scaling->y - 1);
			if (v_factor < 4096)
				v_factor = 4096;
			if ((v_factor * (scaling->y - 1)) != (ast_video->src_fbinfo.y - 1) * 4096)
				v_factor += 1;

			//compression x,y
			ast_video_write(ast_video, VIDEO_COMPRESS_H(scaling->x) | VIDEO_COMPRESS_V(scaling->y), AST_VM_COMPRESS_WIN);
			ast_video_write(ast_video, VIDEO_V_SCAL_FACTOR(v_factor) | VIDEO_H_SCAL_FACTOR(h_factor), AST_VM_SCAL_FACTOR);
		}
	} else {// 1:1
		VIDEO_DBG("Scaling Disable\n");
		ast_video_write(ast_video, VIDEO_COMPRESS_H(ast_video->src_fbinfo.x) | VIDEO_COMPRESS_V(ast_video->src_fbinfo.y), AST_VM_COMPRESS_WIN);
		ast_video_write(ast_video, 0x10001000, AST_VM_SCAL_FACTOR);
	}

	//capture x y
	if (ast_video->config->version >= 5) {
		if (ast_video->src_fbinfo.x == 1680)
			ast_video_write(ast_video, VIDEO_CAPTURE_H(1728) | VIDEO_CAPTURE_V(ast_video->src_fbinfo.y), AST_VM_CAPTURE_WIN);
		else
			ast_video_write(ast_video, VIDEO_CAPTURE_H(ast_video->src_fbinfo.x) | VIDEO_CAPTURE_V(ast_video->src_fbinfo.y), AST_VM_CAPTURE_WIN);
	} else {
		ast_video_write(ast_video, VIDEO_CAPTURE_H(ast_video->src_fbinfo.x) | VIDEO_CAPTURE_V(ast_video->src_fbinfo.y), AST_VM_CAPTURE_WIN);
	}


}

static void ast_video_mode_detect_trigger(struct ast_video_data *ast_video)
{
	VIDEO_DBG("\n");

	if (!(ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & VIDEO_CAPTURE_BUSY))
		pr_info("ERROR ~~ Capture Eng busy !! 0x04 : %x\n", ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL));

	init_completion(&ast_video->mode_detect_complete);

	ast_video_write(ast_video, VIDEO_MODE_DETECT_RDY, AST_VIDEO_INT_EN);

	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_DETECT_TRIGGER | VIDEO_INPUT_MODE_CHG_WDT), AST_VIDEO_SEQ_CTRL);

	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_DETECT_TRIGGER, AST_VIDEO_SEQ_CTRL);

	wait_for_completion_interruptible(&ast_video->mode_detect_complete);

	ast_video_write(ast_video, 0, AST_VIDEO_INT_EN);

}

static void ast_video_vga_mode_detect(struct ast_video_data *ast_video, struct ast_mode_detection *mode_detect)
{
	u32 H_Start, H_End, V_Start, V_End;
	u32 H_Temp = 0, V_Temp = 0, RefreshRateIndex, ColorDepthIndex;
	u32 VGA_Scratch_Register_350, VGA_Scratch_Register_354, VGA_Scratch_Register_34C, Color_Depth, Mode_Clock;
	u8 Direct_Mode;

	VIDEO_DBG("\n");

	//set input signal  and Check polarity (video engine prefers negative signal)
	ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) &
								~(VIDEO_DIRT_FATCH | VIDEO_EXT_ADC_ATTRIBUTE)) |
					VIDEO_INTERNAL_DE |
					VIDEO_SO_VSYNC_POLARITY | VIDEO_SO_HSYNC_POLARITY,
					AST_VIDEO_PASS_CTRL);

	ast_video_mode_detect_trigger(ast_video);

	//Enable Watchdog detection
	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_INPUT_MODE_CHG_WDT, AST_VIDEO_SEQ_CTRL);

Redo:
	//for store lock
	ast_video_mode_detect_trigger(ast_video);

	H_Start = VIDEO_GET_HSYNC_LEFT(ast_video_read(ast_video, AST_VIDEO_H_DETECT_STS));
	H_End = VIDEO_GET_HSYNC_RIGHT(ast_video_read(ast_video, AST_VIDEO_H_DETECT_STS));

	V_Start = VIDEO_GET_VSYNC_TOP(ast_video_read(ast_video, AST_VIDEO_V_DETECT_STS));
	V_End = VIDEO_GET_VSYNC_BOTTOM(ast_video_read(ast_video, AST_VIDEO_V_DETECT_STS));


	//Check if cable quality is too bad. If it is bad then we use 0x65 as threshold
	//Because RGB data is arrived slower than H-sync, V-sync. We have to read more times to confirm RGB data is arrived
	if ((abs(H_Temp - H_Start) > 1) || ((H_Start <= 1) || (V_Start <= 1) || (H_Start == 0xFFF) || (V_Start == 0xFFF))) {
		H_Temp = VIDEO_GET_HSYNC_LEFT(ast_video_read(ast_video, AST_VIDEO_H_DETECT_STS));
		V_Temp = VIDEO_GET_VSYNC_TOP(ast_video_read(ast_video, AST_VIDEO_V_DETECT_STS));
		goto Redo;
	}

//	VIDEO_DBG("H S: %d, E: %d, V S: %d, E: %d\n", H_Start, H_End, V_Start, V_End);

	ast_video_write(ast_video, VIDEO_HSYNC_PIXEL_FIRST_SET(H_Start - 1) | VIDEO_HSYNC_PIXEL_LAST_SET(H_End), AST_VIDEO_TIMING_H);
	ast_video_write(ast_video, VIDEO_VSYNC_PIXEL_FIRST_SET(V_Start) | VIDEO_VSYNC_PIXEL_LAST_SET(V_End + 1), AST_VIDEO_TIMING_V);

	ast_video->src_fbinfo.x = (H_End - H_Start) + 1;
	ast_video->src_fbinfo.y = (V_End - V_Start) + 1;

	VIDEO_DBG("screen mode x:%d, y:%d\n", ast_video->src_fbinfo.x, ast_video->src_fbinfo.y);

	mode_detect->src_x = ast_video->src_fbinfo.x;
	mode_detect->src_y = ast_video->src_fbinfo.y;

	VGA_Scratch_Register_350 = ast_video_read(ast_video, AST_VIDEO_SCRATCH_350);
	VGA_Scratch_Register_34C = ast_video_read(ast_video, AST_VIDEO_SCRATCH_34C);
	VGA_Scratch_Register_354 = ast_video_read(ast_video, AST_VIDEO_SCRATCH_354);

	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) &
					~(VIDEO_SO_VSYNC_POLARITY | VIDEO_SO_HSYNC_POLARITY),
					AST_VIDEO_PASS_CTRL);


	if (((VGA_Scratch_Register_350 & 0xff00) >> 8) == 0xA8) {
		//Driver supports to write display information in scratch register
//		pr_info("Wide Screen Information\n");
		/*
		 * Index 0x94: (VIDEO:1E70:0354)
		 * D[7:0]: HDE D[7:0]
		 * Index 0x95: (VIDEO:1E70:0355)
		 * D[7:0]: HDE D[15:8]
		 * Index 0x96: (VIDEO:1E70:0356)
		 * D[7:0]: VDE D[7:0]
		 * Index 0x97: (VIDEO:1E70:0357)
		 * D[7:0]: VDE D[15:8]
		 */

		Color_Depth = ((VGA_Scratch_Register_350 & 0xff0000) >> 16); //VGA's Color Depth is 0 when real color depth is less than 8
		Mode_Clock = ((VGA_Scratch_Register_350 & 0xff000000) >> 24);
		if (Color_Depth < 15) {
//			pr_info("Color Depth is not 16bpp or higher\n");
			Direct_Mode = 0;
		} else {
//			pr_info("Color Depth is 16bpp or higher\n");
			Direct_Mode = 1;
		}
	} else { //Original mode information
		//Judge if bandwidth is not enough then enable direct mode in internal VGA
		/* Index 0x8E: (VIDEO:1E70:034E)
		 * Mode ID Resolution Notes
		 * 0x2E 640x480
		 * 0x30 800x600
		 * 0x31 1024x768
		 * 0x32 1280x1024
		 * 0x33 1600x1200
		 * 0x34 1920x1200
		 * 0x35 1280x800
		 * 0x36 1440x900
		 * 0x37 1680x1050
		 * 0x38 1920x1080
		 * 0x39 1366x768
		 * 0x3A 1600x900
		 * 0x3B 1152x864
		 * 0x50 320x240
		 * 0x51 400x300
		 * 0x52 512x384
		 * 0x6A 800x600
		 */

		RefreshRateIndex = (VGA_Scratch_Register_34C >> 8) & 0x0F;
		ColorDepthIndex = (VGA_Scratch_Register_34C >> 4) & 0x0F;
//		pr_info("Original mode information\n");
		if ((ColorDepthIndex == 0xe) || (ColorDepthIndex == 0xf)) {
			Direct_Mode = 0;
		} else {
			if (ColorDepthIndex > 2) {
				if ((ast_video->src_fbinfo.x * ast_video->src_fbinfo.y) < (1024 * 768))
					Direct_Mode = 0;
				else
					Direct_Mode = 1;
			} else {
				Direct_Mode = 0;
			}
		}
	}

	if (Direct_Mode) {
		VIDEO_DBG("Direct Mode\n");
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) | VIDEO_DIRT_FATCH | VIDEO_AUTO_FATCH, AST_VIDEO_PASS_CTRL);

//		ast_video_write(ast_video, get_vga_mem_base(), AST_VIDEO_DIRECT_BASE);

		ast_video_write(ast_video, VIDEO_FETCH_TIMING(0) | VIDEO_FETCH_LINE_OFFSET(ast_video->src_fbinfo.x * 4), AST_VIDEO_DIRECT_CTRL);

	} else {
		VIDEO_DBG("Sync Mode\n");
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) & ~VIDEO_DIRT_FATCH, AST_VIDEO_PASS_CTRL);
	}

	//should enable WDT detection every after mode detection
	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_INPUT_MODE_CHG_WDT, AST_VIDEO_SEQ_CTRL);

}

static void ast_video_capture_trigger(struct ast_video_data *ast_video, struct ast_capture_mode *capture_mode)
{
	int timeout = 0;

	VIDEO_DBG("\n");

	if (ast_video->mode_change) {
		capture_mode->mode_change = ast_video->mode_change;
		ast_video->mode_change = 0;
		return;
	}

	switch (capture_mode->engine_idx) {
	case 0:
		init_completion(&ast_video->capture_complete);

		if (capture_mode->differential)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_BCD_CTRL) | VIDEO_BCD_CHG_EN, AST_VIDEO_BCD_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_BCD_CTRL) & ~VIDEO_BCD_CHG_EN, AST_VIDEO_BCD_CTRL);

		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_FORCE_IDLE | VIDEO_COMPRESS_TRIGGER | VIDEO_AUTO_COMPRESS), AST_VIDEO_SEQ_CTRL);
		//If CPU is too fast, pleas read back and trigger
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_CAPTURE_TRIGGER, AST_VIDEO_SEQ_CTRL);

		timeout = wait_for_completion_interruptible_timeout(&ast_video->capture_complete, HZ / 2);

		if (timeout == 0)
			pr_info("Capture timeout sts %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
		break;
	case 1:
//		init_completion(&ast_video->automode_vm_complete);
		if (capture_mode->differential)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_BCD_CTRL) | VIDEO_BCD_CHG_EN, AST_VM_BCD_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_BCD_CTRL) & ~VIDEO_BCD_CHG_EN, AST_VM_BCD_CTRL);
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_TRIGGER | VIDEO_AUTO_COMPRESS), AST_VM_SEQ_CTRL);

		ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) | VIDEO_CAPTURE_TRIGGER, AST_VM_SEQ_CTRL);
		udelay(10);
//AST_G5 Issue in isr bit 19, so use polling mode for wait engine idle
#if 1
		timeout = 0;
		while (1) {
			timeout++;
			if ((ast_video_read(ast_video, AST_VM_SEQ_CTRL) & 0x50000) == 0x50000)
				break;

			mdelay(1);
			if (timeout > 100)
				break;
		}

		if (timeout >= 100)
			pr_info("Engine hang time out\n");

//			pr_info("0 isr %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
		//must clear it
		ast_video_write(ast_video, (ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_TRIGGER)), AST_VM_SEQ_CTRL);
//			pr_info("1 isr %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
#else
		timeout = wait_for_completion_interruptible_timeout(&ast_video->automode_vm_complete, 10 * HZ);

		if (timeout == 0) {
			pr_info("compression timeout sts %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
//			return 0;
		} else {
			pr_info("%x size = %x\n", ast_video_read(ast_video, 0x270), ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END));
//			return ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END);
		}
#endif
		break;
	}

	if (ast_video->mode_change) {
		capture_mode->mode_change = ast_video->mode_change;
		ast_video->mode_change = 0;
	}

}

static void ast_video_compression_trigger(struct ast_video_data *ast_video, struct ast_compression_mode *compression_mode)
{
	int timeout = 0;
	int total_frames = 0;

	VIDEO_DBG("\n");

	if (ast_video->mode_change) {
		compression_mode->mode_change = ast_video->mode_change;
		ast_video->mode_change = 0;
		return;
	}

	switch (compression_mode->engine_idx) {
	case 0:
		init_completion(&ast_video->compression_complete);
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_FORCE_IDLE | VIDEO_COMPRESS_TRIGGER | VIDEO_AUTO_COMPRESS), AST_VIDEO_SEQ_CTRL);
		//If CPU is too fast, pleas read back and trigger
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_COMPRESS_TRIGGER, AST_VIDEO_SEQ_CTRL);

		timeout = wait_for_completion_interruptible_timeout(&ast_video->compression_complete, HZ / 2);

		if (timeout == 0) {
			pr_info("compression timeout sts %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
			compression_mode->total_size = 0;
			compression_mode->block_count = 0;
		} else {
			compression_mode->total_size = ast_video_read(ast_video, AST_VIDEO_COMPRESS_DATA_COUNT);
			compression_mode->block_count = ast_video_read(ast_video, AST_VIDEO_COMPRESS_BLOCK_COUNT) >> 16;

			if (ast_video->config->version == 6) {
				if (ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) & G6_VIDEO_MULTI_JPEG_MODE) {
//					ast_video_write(ast_video, (ast_video_read(ast_video, (AST_VIDEO_SEQ_CTRL) & ~(G5_VIDEO_COMPRESS_JPEG_MODE | VIDEO_CAPTURE_MULTI_FRAME))
//								| VIDEO_AUTO_COMPRESS, AST_VIDEO_SEQ_CTRL);
					VIDEO_DBG("done VR[400]=0x%x, VR[404]=0x%x\n",
							ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM),
							ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM+4));

					total_frames = ((ast_video_read(ast_video, AST_VIDEO_PASS_CTRL)>>24)&0x3f)+1;
					pr_info("total frames=%d\n", total_frames);
					if (total_frames > 1) {
						pr_info("TOOD ~~~~\n");
					} else {
//						compression_mode->frame[0].dwOffsetInBytes = 0;
//						compression_mode->frame[0].dwSizeInBytes = ast_video_read(ast_video, AST_VIDEO_JPEG_COUNT);
					}
				} else if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & G5_VIDEO_COMPRESS_JPEG_MODE) {
					compression_mode->total_size = ast_video_read(ast_video, AST_VIDEO_JPEG_COUNT);
				} else {
//					pr_info("%d	compression_mode->total_size %d , block count %d\n",compression_mode->differential, compression_mode->total_size, compression_mode->block_count);
				}
			} else if (ast_video->config->version == 5) {
				if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & G5_VIDEO_COMPRESS_JPEG_MODE) {
					compression_mode->total_size = ast_video_read(ast_video, AST_VIDEO_JPEG_COUNT);
//					if ((buff[compression_mode->total_size - 2] != 0xff) && (buff[compression_mode->total_size - 1] != 0xd9))
//						pr_info("Error --- %x %x\n", buff[compression_mode->total_size - 2], buff[compression_mode->total_size - 1]);
//					pr_info("jpeg %d compression_mode->total_size %d , block count %d\n",compression_mode->differential, compression_mode->total_size, compression_mode->block_count);
				} else {
//					pr_info("%d	compression_mode->total_size %d , block count %d\n",compression_mode->differential, compression_mode->total_size, compression_mode->block_count);
				}
			} else {
				if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & VIDEO_COMPRESS_JPEG_MODE) {
					compression_mode->total_size = ast_video_read(ast_video, AST_VIDEO_JPEG_COUNT);
//					if ((buff[compression_mode->total_size - 2] != 0xff) && (buff[compression_mode->total_size - 1] != 0xd9)) {
//						pr_info("Error --- %x %x\n", buff[compression_mode->total_size - 2], buff[compression_mode->total_size - 1]);
//					}
//					pr_info("jpeg %d compression_mode->total_size %d , block count %d\n",compression_mode->differential, compression_mode->total_size, compression_mode->block_count);
				} else {
//					pr_info("%d	compression_mode->total_size %d , block count %d\n",compression_mode->differential, compression_mode->total_size, compression_mode->block_count);
				}
			}
		}

		break;
	case 1:
//		init_completion(&ast_video->automode_vm_complete);
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_TRIGGER | VIDEO_AUTO_COMPRESS), AST_VM_SEQ_CTRL);

		ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) | VIDEO_COMPRESS_TRIGGER, AST_VM_SEQ_CTRL);
		udelay(10);
//AST_G5 Issue in isr bit 19, so use polling mode for wait engine idle
#if 1
		timeout = 0;
		while (1) {
			timeout++;
			if ((ast_video_read(ast_video, AST_VM_SEQ_CTRL) & 0x50000) == 0x50000)
				break;

			mdelay(1);
			if (timeout > 100)
				break;
		}

		if (timeout >= 100) {
			pr_info("Engine hang time out\n");
			compression_mode->total_size = 0;
			compression_mode->block_count = 0;
		} else {
			compression_mode->total_size = ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END);
			compression_mode->block_count = ast_video_read(ast_video, AST_VM_COMPRESS_BLOCK_COUNT);
		}

//			pr_info("0 isr %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
		//must clear it
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~VIDEO_COMPRESS_TRIGGER, AST_VM_SEQ_CTRL);
//			pr_info("1 isr %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
#else
		timeout = wait_for_completion_interruptible_timeout(&ast_video->automode_vm_complete, 10 * HZ);

		if (timeout == 0) {
			pr_info("compression timeout sts %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
//			return 0;
		} else {
			pr_info("%x size = %x\n", ast_video_read(ast_video, 0x270), ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END));
//			return ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END);
		}
#endif
		break;
	}

	if (ast_video->mode_change) {
		compression_mode->mode_change = ast_video->mode_change;
		ast_video->mode_change = 0;
	} else
		compression_mode->mode_change = 0;

}

/*return compression size */
static void ast_video_auto_mode_trigger(struct ast_video_data *ast_video, struct ast_auto_mode *auto_mode)
{
	int timeout = 0;

	VIDEO_DBG("\n");

	if (ast_video->mode_change) {
		auto_mode->mode_change = ast_video->mode_change;
		ast_video->mode_change = 0;
		return;
	}

	switch (auto_mode->engine_idx) {
	case 0:
		init_completion(&ast_video->automode_complete);

		if (auto_mode->differential)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_BCD_CTRL) | VIDEO_BCD_CHG_EN, AST_VIDEO_BCD_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_BCD_CTRL) & ~VIDEO_BCD_CHG_EN, AST_VIDEO_BCD_CTRL);

		ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_FORCE_IDLE | VIDEO_COMPRESS_TRIGGER)) | VIDEO_AUTO_COMPRESS, AST_VIDEO_SEQ_CTRL);
		//If CPU is too fast, pleas read back and trigger
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_COMPRESS_TRIGGER | VIDEO_CAPTURE_TRIGGER, AST_VIDEO_SEQ_CTRL);

		timeout = wait_for_completion_interruptible_timeout(&ast_video->automode_complete, HZ / 2);

		if (timeout == 0) {
			pr_info("auto compression timeout sts %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
			auto_mode->total_size = 0;
			auto_mode->block_count = 0;
		} else {
			auto_mode->total_size = ast_video_read(ast_video, AST_VIDEO_COMPRESS_DATA_COUNT);
			auto_mode->block_count = ast_video_read(ast_video, AST_VIDEO_COMPRESS_BLOCK_COUNT) >> 16;
			if (ast_video->config->version >= 5) {
				if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & G5_VIDEO_COMPRESS_JPEG_MODE) {
					auto_mode->total_size = ast_video_read(ast_video, AST_VIDEO_JPEG_COUNT);
//					if ((buff[auto_mode->total_size - 2] != 0xff) && (buff[auto_mode->total_size - 1] != 0xd9))
//						pr_info("Error --- %x %x\n", buff[auto_mode->total_size - 2], buff[auto_mode->total_size - 1]);
//					pr_info("jpeg %d auto_mode->total_size %d , block count %d\n",auto_mode->differential, auto_mode->total_size, auto_mode->block_count);
				} else {
//					pr_info("%d	auto_mode->total_size %d , block count %d\n",auto_mode->differential, auto_mode->total_size, auto_mode->block_count);
				}
			} else {
				if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & VIDEO_COMPRESS_JPEG_MODE) {
					auto_mode->total_size = ast_video_read(ast_video, AST_VIDEO_JPEG_COUNT);
//					if ((buff[auto_mode->total_size - 2] != 0xff) && (buff[auto_mode->total_size - 1] != 0xd9)) {
//						pr_info("Error --- %x %x\n", buff[auto_mode->total_size - 2], buff[auto_mode->total_size - 1]);
//					}
//					pr_info("jpeg %d auto_mode->total_size %d , block count %d\n",auto_mode->differential, auto_mode->total_size, auto_mode->block_count);
				} else {
//					pr_info("%d	auto_mode->total_size %d , block count %d\n",auto_mode->differential, auto_mode->total_size, auto_mode->block_count);
				}
			}
		}

		break;
	case 1:
//			init_completion(&ast_video->automode_vm_complete);
		if (auto_mode->differential)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_BCD_CTRL) | VIDEO_BCD_CHG_EN, AST_VM_BCD_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_BCD_CTRL) & ~VIDEO_BCD_CHG_EN, AST_VM_BCD_CTRL);
		ast_video_write(ast_video, (ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_TRIGGER)) | VIDEO_AUTO_COMPRESS, AST_VM_SEQ_CTRL);

		ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) | VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_TRIGGER, AST_VM_SEQ_CTRL);
		udelay(10);
//AST_G5 Issue in isr bit 19, so use polling mode for wait engine idle
#if 1
		timeout = 0;
		while (1) {
			timeout++;
			if ((ast_video_read(ast_video, AST_VM_SEQ_CTRL) & 0x50000) == 0x50000)
				break;

			mdelay(1);
			if (timeout > 100)
				break;
		}

		if (timeout >= 100) {
			pr_info("Engine hang time out\n");
			auto_mode->total_size = 0;
			auto_mode->block_count = 0;
		} else {
			auto_mode->total_size = ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END);
			auto_mode->block_count = ast_video_read(ast_video, AST_VM_COMPRESS_BLOCK_COUNT);
		}

//			pr_info("0 isr %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
		//must clear it
		ast_video_write(ast_video, (ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_TRIGGER)), AST_VM_SEQ_CTRL);
//			pr_info("1 isr %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
#else
		timeout = wait_for_completion_interruptible_timeout(&ast_video->automode_vm_complete, 10 * HZ);

		if (timeout == 0) {
			pr_info("compression timeout sts %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
//			return 0;
		} else {
			pr_info("%x size = %x\n", ast_video_read(ast_video, 0x270), ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END));
//			return ast_video_read(ast_video, AST_VM_COMPRESS_FRAME_END);
		}
#endif
		break;
	}

	if (ast_video->mode_change) {
		auto_mode->mode_change = ast_video->mode_change;
		ast_video->mode_change = 0;
	}

}

static void ast_video_multi_jpeg_trigger(struct ast_video_data *ast_video, struct aspeed_multi_jpeg_config *multi_jpeg)
{
	u32 yuv_shift;
	u32 yuv_msk;
	u32 scan_lines;
	int timeout = 0;
	u32 x0;
	u32 y0;
	int i = 0;
	u32 dw_w_h;
	u32 start_addr;
	u32 multi_jpeg_data = 0;
	u32 VR044 = ast_video_read(ast_video, AST_VIDEO_SOURCE_BUFF0);

	init_completion(&ast_video->compression_complete);

	scan_lines = ast_video_read(ast_video, AST_VIDEO_SOURCE_SCAN_LINE);

	if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & VIDEO_COMPRESS_FORMAT(YUV420)) {
		// YUV 420
		VIDEO_DBG("Debug: YUV420\n");
		yuv_shift = 4;
		yuv_msk = 0xf;
	} else {
		// YUV 444
		VIDEO_DBG("Debug: YUV444\n");
		yuv_shift = 3;
		yuv_msk = 0x7;
	}

	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) | G6_VIDEO_MULTI_JPEG_FLAG_MODE |
			(G6_VIDEO_JPEG__COUNT(multi_jpeg->multi_jpeg_frames - 1) | G6_VIDEO_MULTI_JPEG_MODE), AST_VIDEO_PASS_CTRL);

	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_BCD_CTRL) & ~VIDEO_BCD_CHG_EN, AST_VIDEO_BCD_CTRL);

	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) | VIDEO_CTRL_ADDRESS_MAP_MULTI_JPEG, AST_VIDEO_CTRL);

	for (i = 0; i < multi_jpeg->multi_jpeg_frames; i++) {
		VIDEO_DBG("Debug: Before: [%d]: x: %#x y: %#x w: %#x h: %#x\n", i,
			multi_jpeg->frame[i].wXPixels, multi_jpeg->frame[i].wYPixels,
			multi_jpeg->frame[i].wWidthPixels, multi_jpeg->frame[i].wHeightPixels);
		x0 = multi_jpeg->frame[i].wXPixels;
		y0 = multi_jpeg->frame[i].wYPixels;
		dw_w_h = SET_FRAME_W_H(multi_jpeg->frame[i].wWidthPixels, multi_jpeg->frame[i].wHeightPixels);
		start_addr = VR044 + (scan_lines * y0) + ((256 * x0) / (1 << yuv_shift));
		VIDEO_DBG("VR%x dw_w_h: %#x, VR%x : addr : %#x, x0 %d, y0 %d\n",
				AST_VIDEO_MULTI_JPEG_SRAM + (8 * i), dw_w_h,
				AST_VIDEO_MULTI_JPEG_SRAM + (8 * i) + 4, start_addr, x0, y0);
		ast_video_write(ast_video, dw_w_h, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i));
		ast_video_write(ast_video, start_addr, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i) + 4);
	}

	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_FORCE_IDLE | VIDEO_COMPRESS_TRIGGER), AST_VIDEO_SEQ_CTRL);
	//set mode for multi-jpeg mode VR004[5:3]
	ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~VIDEO_AUTO_COMPRESS)
				| VIDEO_CAPTURE_MULTI_FRAME | G5_VIDEO_COMPRESS_JPEG_MODE, AST_VIDEO_SEQ_CTRL);

	//If CPU is too fast, pleas read back and trigger
	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_COMPRESS_TRIGGER, AST_VIDEO_SEQ_CTRL);

	timeout = wait_for_completion_interruptible_timeout(&ast_video->compression_complete, HZ / 2);

	if (timeout == 0) {
		pr_info("multi compression timeout sts %x\n", ast_video_read(ast_video, AST_VIDEO_INT_STS));
		multi_jpeg->multi_jpeg_frames = 0;
	} else {
		VIDEO_DBG("400 %x , 404 %x\n", ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM), ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM + 4));
		VIDEO_DBG("408 %x , 40c %x\n", ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM + 8), ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM + 0xC));
		for (i = 0; i < multi_jpeg->multi_jpeg_frames; i++) {
			multi_jpeg_data = ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i) + 4);
			if (multi_jpeg_data & BIT(7)) {
				multi_jpeg->frame[i].dwSizeInBytes = ast_video_read(ast_video, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i)) & 0xffffff;
				multi_jpeg->frame[i].dwOffsetInBytes = (multi_jpeg_data & ~BIT(7)) >> 1;
			} else {
				multi_jpeg->frame[i].dwSizeInBytes = 0;
			}
			VIDEO_DBG("[%d] size %d , dwOffsetInBytes %x\n", i, multi_jpeg->frame[i].dwSizeInBytes, multi_jpeg->frame[i].dwOffsetInBytes);
		}
	}

	ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~(G5_VIDEO_COMPRESS_JPEG_MODE | VIDEO_CAPTURE_MULTI_FRAME))
			| VIDEO_AUTO_COMPRESS, AST_VIDEO_SEQ_CTRL);
	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) &
			~(G6_VIDEO_FRAME_CT_MASK | G6_VIDEO_MULTI_JPEG_MODE), AST_VIDEO_PASS_CTRL);

}

static void ast_video_multi_jpeg_automode_trigger(struct ast_video_data *ast_video, struct aspeed_multi_jpeg_config *multi_jpeg)
{
	struct ast_auto_mode auto_mode;
	u32 yuv_shift = 0;
	u32 bonding_x, bonding_y;
	u32 x, y;
	int i, j = 0;
	u8 *bcd_buf = (u8 *)ast_video->bcd_virt;
	u32 max_x, min_x, max_y, min_y;

	auto_mode.engine_idx = 0;
	auto_mode.mode_change = 0;

	//bcd : 0 first fram.
	if (multi_jpeg->multi_jpeg_frames)
		auto_mode.differential = 1;
	else	//first frame
		auto_mode.differential = 0;

	VIDEO_DBG("multi_jpeg_frames %d\n", multi_jpeg->multi_jpeg_frames);
	//do aspeed mode first
	ast_video_auto_mode_trigger(ast_video, &auto_mode);
	if (ast_video->mode_change)
		return;

	if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & VIDEO_COMPRESS_FORMAT(YUV420)) {
		// YUV 420
		VIDEO_DBG("Debug: YUV420\n");
		yuv_shift = 4;
	} else {
		// YUV 444
		VIDEO_DBG("Debug: YUV444\n");
		yuv_shift = 3;
	}

	VIDEO_DBG("w %d, h %d bcd phy [%x]\n", ast_video->src_fbinfo.x, ast_video->src_fbinfo.y, (u32)ast_video->bcd_phy);

	if (auto_mode.differential) {
		//find bonding box
		multi_jpeg->multi_jpeg_frames = 1;
		bonding_x = ast_video_read(ast_video, AST_VIDEO_BONDING_X);
		bonding_y = ast_video_read(ast_video, AST_VIDEO_BONDING_Y);
		VIDEO_DBG("bonding box %x , %x\n", bonding_x, bonding_y);
#if 1
		x = ast_video->src_fbinfo.x / (1 << yuv_shift);
		y = ast_video->src_fbinfo.y / (1 << yuv_shift);

		min_x = 0x3ff;
		min_y = 0x3ff;
		max_x = 0;
		max_y = 0;
		VIDEO_DBG("block x %d ,y %d\n", x, y);

		for (j = 0; j < y; j++) {
			for (i = 0; i < x; i++) {
				if ((*(bcd_buf + (x*j) + i) & 0xf) != 0xf) {
//					VIDEO_DBG("[%x]: x: %d ,y: %d : data : %x\n",(x*j) + i, i, j, *(bcd_buf + (x*j) + i));
					if (i < min_x)
						min_x = i;
					if (i > max_x)
						max_x = i;
					if (j < min_y)
						min_y = j;
					if (j > max_y)
						max_y = j;
				}
			}
		}
		bonding_x = (max_x << 16) | min_x;
		bonding_y = (max_y << 16) | min_y;
		VIDEO_DBG("bonding box %x , %x\n", bonding_x, bonding_y);
#endif
		if ((bonding_y == 0x3ff) && (bonding_x == 0x3ff)) {
			multi_jpeg->frame[0].dwSizeInBytes = 0;
			return;
		}
		multi_jpeg->frame[0].wXPixels = (bonding_x & 0xffff) * (1 << yuv_shift);
		VIDEO_DBG("x : %d, %d, yuv block size %d\n", multi_jpeg->frame[0].wXPixels, (bonding_x & 0xffff), (1 << yuv_shift));
		multi_jpeg->frame[0].wYPixels = (bonding_y & 0xffff) * (1 << yuv_shift);
		VIDEO_DBG("y : %d, %d, yuv block size %d\n", multi_jpeg->frame[0].wYPixels, (bonding_y & 0xffff), (1 << yuv_shift));
		multi_jpeg->frame[0].wWidthPixels = ((bonding_x >> 16) + 1 - (bonding_x & 0xffff)) * (1 << yuv_shift);
		multi_jpeg->frame[0].wHeightPixels = ((bonding_y >> 16) + 1 - (bonding_y & 0xffff)) * (1 << yuv_shift);
		VIDEO_DBG("w %d , h : %d\n", multi_jpeg->frame[0].wWidthPixels, multi_jpeg->frame[0].wHeightPixels);
	} else {
		//first frame
		multi_jpeg->multi_jpeg_frames = 1;
		multi_jpeg->frame[0].wXPixels = 0;
		multi_jpeg->frame[0].wYPixels = 0;
		multi_jpeg->frame[0].wWidthPixels = ast_video->src_fbinfo.x;
		multi_jpeg->frame[0].wHeightPixels = ast_video->src_fbinfo.y;
	}
	ast_video_multi_jpeg_trigger(ast_video, multi_jpeg);
}

static void ast_video_mode_detect_info(struct ast_video_data *ast_video)

{
	u32 H_Start, H_End, V_Start, V_End;

	H_Start = VIDEO_GET_HSYNC_LEFT(ast_video_read(ast_video, AST_VIDEO_H_DETECT_STS));
	H_End = VIDEO_GET_HSYNC_RIGHT(ast_video_read(ast_video, AST_VIDEO_H_DETECT_STS));

	V_Start = VIDEO_GET_VSYNC_TOP(ast_video_read(ast_video, AST_VIDEO_V_DETECT_STS));
	V_End = VIDEO_GET_VSYNC_BOTTOM(ast_video_read(ast_video, AST_VIDEO_V_DETECT_STS));

	VIDEO_DBG("Get H_Start = %d, H_End = %d, V_Start = %d, V_End = %d\n", H_Start, H_End, V_Start, V_End);

	ast_video->src_fbinfo.x = (H_End - H_Start) + 1;
	ast_video->src_fbinfo.y = (V_End - V_Start) + 1;
	VIDEO_DBG("source : x = %d, y = %d , color mode = %x\n", ast_video->src_fbinfo.x, ast_video->src_fbinfo.y, ast_video->src_fbinfo.color_mode);
}


static irqreturn_t ast_video_isr(int this_irq, void *dev_id)
{
	u32 status;
	u32 swap0, swap1;
	struct ast_video_data *ast_video = dev_id;

	status = ast_video_read(ast_video, AST_VIDEO_INT_STS);

	VIDEO_DBG("%x\n", status);

	if (status & VIDEO_MODE_DETECT_RDY) {
		ast_video_write(ast_video, VIDEO_MODE_DETECT_RDY, AST_VIDEO_INT_STS);
		complete(&ast_video->mode_detect_complete);
	}

	if (status & VIDEO_MODE_DETECT_WDT) {
		ast_video->mode_change = 1;
		VIDEO_DBG("mode change\n");
		ast_video_write(ast_video, VIDEO_MODE_DETECT_WDT, AST_VIDEO_INT_STS);
	}

	if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & VIDEO_AUTO_COMPRESS) {
		if ((status & (VIDEO_COMPRESS_COMPLETE | VIDEO_CAPTURE_COMPLETE)) == (VIDEO_COMPRESS_COMPLETE | VIDEO_CAPTURE_COMPLETE)) {
			ast_video_write(ast_video, VIDEO_COMPRESS_COMPLETE | VIDEO_CAPTURE_COMPLETE, AST_VIDEO_INT_STS);
			if (!ast_video->multi_jpeg) {
				swap0 = ast_video_read(ast_video, AST_VIDEO_SOURCE_BUFF0);
				swap1 = ast_video_read(ast_video, AST_VIDEO_SOURCE_BUFF1);
				ast_video_write(ast_video, swap1, AST_VIDEO_SOURCE_BUFF0);
				ast_video_write(ast_video, swap0, AST_VIDEO_SOURCE_BUFF1);
			}
			VIDEO_DBG("auto mode complete\n");
			complete(&ast_video->automode_complete);
		}
	} else {
		if (status & VIDEO_COMPRESS_COMPLETE) {
			ast_video_write(ast_video, VIDEO_COMPRESS_COMPLETE, AST_VIDEO_INT_STS);
			VIDEO_DBG("compress complete swap\n");
			swap0 = ast_video_read(ast_video, AST_VIDEO_SOURCE_BUFF0);
			swap1 = ast_video_read(ast_video, AST_VIDEO_SOURCE_BUFF1);
			ast_video_write(ast_video, swap1, AST_VIDEO_SOURCE_BUFF0);
			ast_video_write(ast_video, swap0, AST_VIDEO_SOURCE_BUFF1);
			complete(&ast_video->compression_complete);
		}
		if (status & VIDEO_CAPTURE_COMPLETE) {
			ast_video_write(ast_video, VIDEO_CAPTURE_COMPLETE, AST_VIDEO_INT_STS);
			VIDEO_DBG("capture complete\n");
			complete(&ast_video->capture_complete);
		}
	}

	return IRQ_HANDLED;
}

#define AST_CRT_ADDR				0x80

static void ast_set_crt_compression(struct ast_video_data *ast_video, struct fb_var_screeninfo *fb_info)
{
	u32 val;

	//if use crt compression, need give capture engine clk and also can't less then 1/4 dram controller clk
	//now set d-pll for 66mhz
	regmap_write(ast_video->scu, 0x028, 0x5c822029);
	regmap_write(ast_video->scu, 0x130, 0x00000580);
	regmap_update_bits(ast_video->scu, AST_SCU_MISC1_CTRL, BIT(20), BIT(20));

	ast_video->src_fbinfo.x = fb_info->xres;
	ast_video->src_fbinfo.y = fb_info->yres;

	//VR008[5] = 1
	//VR008[8]<=0
	ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) | VIDEO_DIRT_FATCH) & ~VIDEO_AUTO_FATCH, AST_VIDEO_PASS_CTRL);

	//VR008[4]<=0 when CRT60[8:7]=10. VR008[4]<=1 when CRT60[8:7]=00.
	//regmap_read(ast_video->gfx, AST_CRT_CTRL1, &val);
//	pr_info("AST_CRT_CTRL1 %x\n", val);
	if (fb_info->bits_per_pixel == 32)
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) & ~VIDEO_16BPP_MODE, AST_VIDEO_PASS_CTRL);
	else if (fb_info->bits_per_pixel == 16)
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) | VIDEO_16BPP_MODE, AST_VIDEO_PASS_CTRL);
	else
		pr_info("error\n");

	//VR00C <= CRT80
	regmap_read(ast_video->gfx, AST_CRT_ADDR, &val);
//	pr_info("AST_CRT_ADDR %x\n", val);
	ast_video_write(ast_video, val, AST_VIDEO_DIRECT_BASE);

	//VR010[14:0] <= CRT84[14:0]
	//var->xres * var->bits_per_pixel /8;
//	regmap_read(ast_video->gfx, AST_CRT_OFFSET, &val);
//	pr_info("AST_CRT_OFFSET %x\n", val);
	val = fb_info->xres * fb_info->bits_per_pixel / 8;
	ast_video_write(ast_video, val, AST_VIDEO_DIRECT_CTRL);

	//VR010[15]<=0 //force VGA blank, don;t have to do
}

static void ast_video_ctrl_init(struct ast_video_data *ast_video)
{
	VIDEO_DBG("\n");

	ast_video_write(ast_video, (u32)ast_video->buff0_phy, AST_VIDEO_SOURCE_BUFF0);
	ast_video_write(ast_video, (u32)ast_video->buff1_phy, AST_VIDEO_SOURCE_BUFF1);
	ast_video_write(ast_video, (u32)ast_video->bcd_phy, AST_VIDEO_BCD_BUFF);
	ast_video_write(ast_video, (u32)ast_video->stream_phy, AST_VIDEO_STREAM_BUFF);
	ast_video_write(ast_video, (u32)ast_video->jpeg_tbl_phy, AST_VIDEO_JPEG_HEADER_BUFF);
	ast_video_write(ast_video, (u32)ast_video->jpeg_tbl_phy, AST_VM_JPEG_HEADER_BUFF);
	ast_video_write(ast_video, (u32)ast_video->jpeg_buf0_phy, AST_VM_SOURCE_BUFF0);
	ast_video_write(ast_video, (u32)ast_video->jpeg_phy, AST_VM_COMPRESS_BUFF);
	ast_video_write(ast_video, 0, AST_VIDEO_COMPRESS_READ);

	//clr int sts
	ast_video_write(ast_video, 0xffffffff, AST_VIDEO_INT_STS);
	ast_video_write(ast_video, 0, AST_VIDEO_BCD_CTRL);

	// =============================  JPEG init ===========================================
	ast_init_jpeg_table(ast_video);
	ast_video_write(ast_video,  VM_STREAM_PKT_SIZE(STREAM_3MB), AST_VM_STREAM_SIZE);
	ast_video_write(ast_video,  0x00080000 | VIDEO_DCT_LUM(4) | VIDEO_DCT_CHROM(4 + 16) | VIDEO_DCT_ONLY_ENCODE, AST_VM_COMPRESS_CTRL);

	//WriteMMIOLong(0x1e700238, 0x00000000);
	//WriteMMIOLong(0x1e70023c, 0x00000000);

	ast_video_write(ast_video, 0x00001E00, AST_VM_SOURCE_SCAN_LINE); //buffer pitch
	ast_video_write(ast_video, 0x00000000, 0x268);
	ast_video_write(ast_video, 0x00001234, 0x280);

	ast_video_write(ast_video, 0x00000000, AST_VM_PASS_CTRL);
	ast_video_write(ast_video, 0x00000000, AST_VM_BCD_CTRL);

	// ===============================================================================


	//Specification define bit 12:13 must always 0;
	ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_PASS_CTRL) &
								~(VIDEO_DUAL_EDGE_MODE | VIDEO_18BIT_SINGLE_EDGE)) |
					VIDEO_DVO_INPUT_DELAY(0x4),
					AST_VIDEO_PASS_CTRL);

	ast_video_write(ast_video, VIDEO_STREAM_PKT_N(STREAM_32_PKTS) |
					VIDEO_STREAM_PKT_SIZE(STREAM_128KB), AST_VIDEO_STREAM_SIZE);


	//rc4 init reset ..
	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) | VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
	ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) & ~VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);

	//CRC/REDUCE_BIT register clear
	ast_video_write(ast_video, 0, AST_VIDEO_CRC1);
	ast_video_write(ast_video, 0, AST_VIDEO_CRC2);
	ast_video_write(ast_video, 0, AST_VIDEO_DATA_TRUNCA);
	ast_video_write(ast_video, 0, AST_VIDEO_COMPRESS_READ);

	ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_MODE_DETECT) & 0xff) |
					VIDEO_MODE_HOR_TOLER(6) |
					VIDEO_MODE_VER_TOLER(6) |
					VIDEO_MODE_HOR_STABLE(2) |
					VIDEO_MODE_VER_STABLE(2) |
					VIDEO_MODE_EDG_THROD(0x65)
					, AST_VIDEO_MODE_DETECT);

	if (ast_video->config->version == 6)
		ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_MODE_DET2) | BIT(13)), AST_VIDEO_MODE_DET2);

}

static void ast_scu_reset_video(struct ast_video_data *ast_video)
{
	reset_control_assert(ast_video->reset);
	udelay(100);
	reset_control_deassert(ast_video->reset);
}

static long ast_video_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int ret = 1;
	struct miscdevice *c = fp->private_data;
	struct ast_video_data *ast_video = dev_get_drvdata(c->this_device);
	struct ast_scaling set_scaling;
	struct ast_video_config video_config;
	struct ast_capture_mode capture_mode;
	struct ast_compression_mode compression_mode;
	struct aspeed_multi_jpeg_config multi_jpeg;
	struct fb_var_screeninfo fb_info;
	int vga_enable = 0;
	int encrypt_en = 0;
	struct ast_mode_detection mode_detection;
	struct ast_auto_mode auto_mode;
	void __user *argp = (void __user *)arg;


	switch (cmd) {
	case AST_VIDEO_RESET:
		ast_scu_reset_video(ast_video);
		//rc4 init reset ..
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) | VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) & ~VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
		ast_video_ctrl_init(ast_video);
		ret = 0;
		break;
	case AST_VIDEO_IOC_GET_VGA_SIGNAL:
		ret = put_user(ast_get_vga_signal(ast_video), (unsigned char __user *)arg);
		break;
	case AST_VIDEO_GET_MEM_SIZE_IOCRX:
		ret = __put_user(ast_video->video_mem_size, (unsigned long __user *)arg);
		break;
	case AST_VIDEO_GET_JPEG_OFFSET_IOCRX:
		ret = __put_user(ast_video->video_jpeg_offset, (unsigned long __user *)arg);
		break;
	case AST_VIDEO_VGA_MODE_DETECTION:
		ret = copy_from_user(&mode_detection, argp, sizeof(struct ast_mode_detection));
		ast_video_vga_mode_detect(ast_video, &mode_detection);
		ret = copy_to_user(argp, &mode_detection, sizeof(struct ast_mode_detection));
		break;
	case AST_VIDEO_ENG_CONFIG:
		ret = copy_from_user(&video_config, argp, sizeof(struct ast_video_config));
		ast_video_set_eng_config(ast_video, &video_config);
		break;
	case AST_VIDEO_SET_SCALING:
		ret = copy_from_user(&set_scaling, argp, sizeof(struct ast_scaling));
		switch (set_scaling.engine) {
		case 0:
			ast_video_set_0_scaling(ast_video, &set_scaling);
			break;
		case 1:
			ast_video_set_1_scaling(ast_video, &set_scaling);
			break;
		}
		break;
	case AST_VIDEO_AUTOMODE_TRIGGER:
		ret = copy_from_user(&auto_mode, argp, sizeof(struct ast_auto_mode));
		ast_video_auto_mode_trigger(ast_video, &auto_mode);
		ret = copy_to_user(argp, &auto_mode, sizeof(struct ast_auto_mode));
		break;
	case AST_VIDEO_CAPTURE_TRIGGER:
		ret = copy_from_user(&capture_mode, argp, sizeof(capture_mode));
		ast_video_capture_trigger(ast_video, &capture_mode);
		ret = copy_to_user(argp, &capture_mode, sizeof(capture_mode));
		break;
	case AST_VIDEO_COMPRESSION_TRIGGER:
		ret = copy_from_user(&compression_mode, argp, sizeof(compression_mode));
		ast_video_compression_trigger(ast_video, &compression_mode);
		ret = copy_to_user(argp, &compression_mode, sizeof(compression_mode));
		break;
	case AST_VIDEO_SET_VGA_DISPLAY:
		ret = __get_user(vga_enable, (int __user *)arg);
		ast_scu_set_vga_display(ast_video, vga_enable);
		break;
	case AST_VIDEO_SET_ENCRYPTION:
		ret = __get_user(encrypt_en, (int __user *)arg);
		if (encrypt_en)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_COMPRESS_CTRL) | VIDEO_ENCRYP_ENABLE, AST_VIDEO_COMPRESS_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_COMPRESS_CTRL) & ~VIDEO_ENCRYP_ENABLE, AST_VIDEO_COMPRESS_CTRL);
		break;
	case AST_VIDEO_SET_ENCRYPTION_KEY:
		memset(ast_video->EncodeKeys, 0, 256);
		//due to system have enter key must be remove
		ret = copy_from_user(ast_video->EncodeKeys, argp, 256 - 1);
		pr_info("encryption key '%s'\n", ast_video->EncodeKeys);
//			memcpy(ast_video->EncodeKeys, key, strlen(key) - 1);
		ast_video_encryption_key_setup(ast_video);
		ret = 0;
		break;
	case AST_VIDEO_SET_CRT_COMPRESSION:
		ret = copy_from_user(&fb_info, argp, sizeof(struct fb_var_screeninfo));
		ast_set_crt_compression(ast_video, &fb_info);
		ret = 0;
		break;
	case AST_VIDEO_MULTIJPEG_AUTOMODE_TRIGGER:
		ret = copy_from_user(&multi_jpeg, argp, sizeof(multi_jpeg));
		ast_video_multi_jpeg_automode_trigger(ast_video, &multi_jpeg);
		ret = copy_to_user(argp, &multi_jpeg, sizeof(multi_jpeg));
		break;
	case AST_VIDEO_MULTIJPEG_TRIGGER:
		ret = copy_from_user(&multi_jpeg, argp, sizeof(multi_jpeg));
		ast_video_multi_jpeg_trigger(ast_video, &multi_jpeg);
		ret = copy_to_user(argp, &multi_jpeg, sizeof(multi_jpeg));
		break;
	default:
		ret = 3;
		break;
	}
	return ret;

}

/** @note munmap handler is done by vma close handler */
static int ast_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct miscdevice *c = file->private_data;
	struct ast_video_data *ast_video = dev_get_drvdata(c->this_device);
	size_t size = vma->vm_end - vma->vm_start;

	vma->vm_private_data = ast_video;

	if (PAGE_ALIGN(size) > ast_video->video_mem_size) {
		pr_err("required length exceed the size of physical sram (%x)\n", ast_video->video_mem_size);
		return -EAGAIN;
	}

	if ((ast_video->stream_phy + (vma->vm_pgoff << PAGE_SHIFT) + size)
		> (ast_video->stream_phy + ast_video->video_mem_size)) {
		pr_err("required sram range exceed the size of phisical sram\n");
		return -EAGAIN;
	}

	vma->vm_flags |= VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start,
						   ((u32)ast_video->stream_phy >> PAGE_SHIFT),
						   size,
						   vma->vm_page_prot)) {
		pr_err("remap_pfn_range faile at %s()\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

static int ast_video_open(struct inode *inode, struct file *file)
{
	struct miscdevice *c = file->private_data;
	struct ast_video_data *ast_video = dev_get_drvdata(c->this_device);

	VIDEO_DBG("\n");

	ast_video->is_open = true;

	return 0;

}

static int ast_video_release(struct inode *inode, struct file *file)
{
	struct miscdevice *c = file->private_data;
	struct ast_video_data *ast_video = dev_get_drvdata(c->this_device);

	VIDEO_DBG("\n");

	ast_video->is_open = false;
	return 0;
}

static const struct file_operations ast_video_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.unlocked_ioctl		= ast_video_ioctl,
	.open			= ast_video_open,
	.release		= ast_video_release,
	.mmap			= ast_video_mmap,
};

struct miscdevice ast_video_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ast-video",
	.fops = &ast_video_fops,
};

/************************************************** SYS FS **************************************************************/
static ssize_t vga_display_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ast_video_data *ast_video = dev_get_drvdata(dev);

	return sprintf(buf, "%d: %s\n", ast_scu_get_vga_display(ast_video), ast_scu_get_vga_display(ast_video) ? "Enable" : "Disable");
}

static ssize_t vga_display_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
	struct ast_video_data *ast_video = dev_get_drvdata(dev);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		pr_err("%s: input invalid", __func__);

	if (val)
		ast_scu_set_vga_display(ast_video, 1);
	else
		ast_scu_set_vga_display(ast_video, 0);

	return count;
}

static DEVICE_ATTR_RW(vga_display);

static ssize_t video_reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
	struct ast_video_data *ast_video = dev_get_drvdata(dev);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		pr_err("%s: input invalid", __func__);

	if (val) {
		ast_scu_reset_video(ast_video);
		//rc4 init reset ..
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) | VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) & ~VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
		ast_video_ctrl_init(ast_video);
	}

	return count;
}

static DEVICE_ATTR_WO(video_reset);

static ssize_t video_mode_detect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct ast_video_data *ast_video = dev_get_drvdata(dev);

	if (ret < 0)
		return ret;

	ast_video_mode_detect_info(ast_video);

	return sprintf(buf, "%i\n", ret);
}

static ssize_t video_mode_detect_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
	struct ast_video_data *ast_video = dev_get_drvdata(dev);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		pr_err("%s: input invalid", __func__);

	if (val)
		ast_video_mode_detect_trigger(ast_video);

	return count;
}

static DEVICE_ATTR_RW(video_mode_detect);

static struct attribute *ast_video_attributes[] = {
	&dev_attr_vga_display.attr,
	&dev_attr_video_reset.attr,
	&dev_attr_video_mode_detect.attr,
	NULL
};

static const struct attribute_group video_attribute_group = {
	.attrs = ast_video_attributes
};

/**************************   Vudeo SYSFS  **********************************************************/
enum ast_video_trigger_mode {
	VIDEO_CAPTURE_MODE = 0,
	VIDEO_COMPRESSION_MODE,
	VIDEO_BUFFER_MODE,
};

static u8 ast_get_trigger_mode(struct ast_video_data *ast_video, u8 eng_idx)
{
	//VR0004[3:5] 00:capture/compression/buffer
	u32 mode = 0;

	switch (eng_idx) {
	case 0:
		mode = ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & (VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS);
		if (mode == 0)
			return VIDEO_CAPTURE_MODE;
		if (mode == VIDEO_AUTO_COMPRESS)
			return VIDEO_COMPRESSION_MODE;
		if (mode == (VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS))
			return VIDEO_BUFFER_MODE;
		pr_info("ERROR Mode\n");
		break;
	case 1:
		mode = ast_video_read(ast_video, AST_VM_SEQ_CTRL) & (VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS);
		if (mode == 0)
			return VIDEO_CAPTURE_MODE;
		if (mode == VIDEO_AUTO_COMPRESS)
			return VIDEO_COMPRESSION_MODE;
		if (mode == (VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS))
			return VIDEO_BUFFER_MODE;
		pr_info("ERROR Mode\n");
		break;
	}

	return mode;
}

static void ast_set_trigger_mode(struct ast_video_data *ast_video, u8 eng_idx, u8 mode)
{
	//VR0004[3:5] 00/01/11:capture/frame/stream
	switch (eng_idx) {
	case 0:	//video 1
		if (mode == VIDEO_CAPTURE_MODE)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS), AST_VIDEO_SEQ_CTRL);
		else if (mode == VIDEO_COMPRESSION_MODE)
			ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_AUTO_COMPRESS) & ~(VIDEO_CAPTURE_MULTI_FRAME), AST_VIDEO_SEQ_CTRL);
		else if (mode == VIDEO_BUFFER_MODE)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS, AST_VIDEO_SEQ_CTRL);
		else
			pr_info("ERROR Mode\n");
		break;
	case 1:	//video M
		if (mode == VIDEO_CAPTURE_MODE)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~(VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS), AST_VM_SEQ_CTRL);
		else if (mode == VIDEO_COMPRESSION_MODE)
			ast_video_write(ast_video, (ast_video_read(ast_video, AST_VM_SEQ_CTRL) | VIDEO_AUTO_COMPRESS) & ~(VIDEO_CAPTURE_MULTI_FRAME), AST_VM_SEQ_CTRL);
		else if (mode == VIDEO_BUFFER_MODE)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) | VIDEO_CAPTURE_MULTI_FRAME | VIDEO_AUTO_COMPRESS, AST_VM_SEQ_CTRL);
		else
			pr_info("ERROR Mode\n");
		break;
	}
}

static u8 ast_get_compress_yuv_mode(struct ast_video_data *ast_video, u8 eng_idx)
{
	switch (eng_idx) {
	case 0:
		return VIDEO_GET_COMPRESS_FORMAT(ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL));
	case 1:
		return VIDEO_GET_COMPRESS_FORMAT(ast_video_read(ast_video, AST_VM_SEQ_CTRL));
	}
	return 0;
}

static void ast_set_compress_yuv_mode(struct ast_video_data *ast_video, u8 eng_idx, u8 yuv_mode)
{
	int i, base = 0;
	u32 *tlb_table = ast_video->jpeg_tbl_virt;

	switch (eng_idx) {
	case 0:	//video 1
		if (yuv_mode)	//YUV420
			ast_video_write(ast_video, (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~VIDEO_COMPRESS_FORMAT_MASK) | VIDEO_COMPRESS_FORMAT(YUV420), AST_VIDEO_SEQ_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~VIDEO_COMPRESS_FORMAT_MASK, AST_VIDEO_SEQ_CTRL);
		break;
	case 1:	//video M
		if (yuv_mode)	//YUV420
			ast_video_write(ast_video, (ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~VIDEO_COMPRESS_FORMAT_MASK) | VIDEO_COMPRESS_FORMAT(YUV420), AST_VM_SEQ_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~VIDEO_COMPRESS_FORMAT_MASK, AST_VM_SEQ_CTRL);

		for (i = 0; i < 12; i++) {
			base = (256 * i);
			if (yuv_mode)	//yuv420
				tlb_table[base + 46] = 0x00220103; //for YUV420 mode
			else
				tlb_table[base + 46] = 0x00110103; //for YUV444 mode)
		}

		break;
	}
}

static u8 ast_get_compress_jpeg_mode(struct ast_video_data *ast_video, u8 eng_idx)
{
	switch (eng_idx) {
	case 0:
		if ((ast_video->config->version == 5) || (ast_video->config->version == 6)) {
			if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & G5_VIDEO_COMPRESS_JPEG_MODE)
				return 1;
			else
				return 0;
		} else {
			if (ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & VIDEO_COMPRESS_JPEG_MODE)
				return 1;
			else
				return 0;
		}
		break;
	case 1:
		if ((ast_video->config->version == 5) || (ast_video->config->version == 6)) {
			if (ast_video_read(ast_video, AST_VM_SEQ_CTRL) & G5_VIDEO_COMPRESS_JPEG_MODE)
				return 1;
			else
				return 0;
		} else {
			if (ast_video_read(ast_video, AST_VM_SEQ_CTRL) & VIDEO_COMPRESS_JPEG_MODE)
				return 1;
			else
				return 0;
		}
		break;
	}
	return 0;
}

static void ast_set_compress_jpeg_mode(struct ast_video_data *ast_video, u8 eng_idx, u8 jpeg_mode)
{
	switch (eng_idx) {
	case 0:	//video 1
		if (jpeg_mode) {
			if ((ast_video->config->version == 5) || (ast_video->config->version == 6))
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | G5_VIDEO_COMPRESS_JPEG_MODE, AST_VIDEO_SEQ_CTRL);
			else
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) | VIDEO_COMPRESS_JPEG_MODE, AST_VIDEO_SEQ_CTRL);
		} else {
			if ((ast_video->config->version == 5) || (ast_video->config->version == 6))
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~G5_VIDEO_COMPRESS_JPEG_MODE, AST_VIDEO_SEQ_CTRL);
			else
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_SEQ_CTRL) & ~VIDEO_COMPRESS_JPEG_MODE, AST_VIDEO_SEQ_CTRL);

		}
		break;
	case 1:	//video M
		if (jpeg_mode) {
			if ((ast_video->config->version == 5) || (ast_video->config->version == 6))
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) | G5_VIDEO_COMPRESS_JPEG_MODE, AST_VM_SEQ_CTRL);
			else
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) | VIDEO_COMPRESS_JPEG_MODE, AST_VM_SEQ_CTRL);
		} else {
			if ((ast_video->config->version == 5) || (ast_video->config->version == 6))
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~G5_VIDEO_COMPRESS_JPEG_MODE, AST_VM_SEQ_CTRL);
			else
				ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_SEQ_CTRL) & ~VIDEO_COMPRESS_JPEG_MODE, AST_VM_SEQ_CTRL);
		}
		break;
	}
}

static u8 ast_get_compress_encrypt_en(struct ast_video_data *ast_video, u8 eng_idx)
{
	switch (eng_idx) {
	case 0:
		if (ast_video_read(ast_video, AST_VIDEO_COMPRESS_CTRL) & VIDEO_ENCRYP_ENABLE)
			return 1;
		else
			return 0;
		break;
	case 1:
		if (ast_video_read(ast_video, AST_VM_COMPRESS_CTRL) & VIDEO_ENCRYP_ENABLE)
			return 1;
		else
			return 0;
		break;
	}

	return 0;
}

static void ast_set_compress_encrypt_en(struct ast_video_data *ast_video, u8 eng_idx, u8 enable)
{
	switch (eng_idx) {
	case 0:	//video 1
		if (enable)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_COMPRESS_CTRL) | VIDEO_ENCRYP_ENABLE, AST_VIDEO_COMPRESS_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_COMPRESS_CTRL) & ~VIDEO_ENCRYP_ENABLE, AST_VIDEO_COMPRESS_CTRL);
		break;
	case 1:	//video M
		if (enable)
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_COMPRESS_CTRL) | VIDEO_ENCRYP_ENABLE, AST_VIDEO_COMPRESS_CTRL);
		else
			ast_video_write(ast_video, ast_video_read(ast_video, AST_VM_COMPRESS_CTRL) & ~VIDEO_ENCRYP_ENABLE, AST_VIDEO_COMPRESS_CTRL);
		break;
	}
}

static u8 *ast_get_compress_encrypt_key(struct ast_video_data *ast_video, u8 eng_idx)
{
	switch (eng_idx) {
	case 0:
		return ast_video->EncodeKeys;
	case 1:
		return ast_video->EncodeKeys;
	}
	return 0;
}

static void ast_set_compress_encrypt_key(struct ast_video_data *ast_video, u8 eng_idx, u8 *key)
{
	switch (eng_idx) {
	case 0:	//video 1
		memset(ast_video->EncodeKeys, 0, 256);
		//due to system have enter key must be remove
		memcpy(ast_video->EncodeKeys, key, strlen(key) - 1);
		ast_video_encryption_key_setup(ast_video);
		break;
	case 1:	//video M
		break;
	}
}

static u8 ast_get_compress_encrypt_mode(struct ast_video_data *ast_video)
{
	if (ast_video_read(ast_video, AST_VIDEO_CTRL) & VIDEO_CTRL_CRYPTO_AES)
		return 1;
	else
		return 0;
}

static void ast_set_compress_encrypt_mode(struct ast_video_data *ast_video, u8 mode)
{
	if (mode)
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) | VIDEO_CTRL_CRYPTO_AES, AST_VIDEO_CTRL);
	else
		ast_video_write(ast_video, ast_video_read(ast_video, AST_VIDEO_CTRL) & ~VIDEO_CTRL_CRYPTO_AES, AST_VIDEO_CTRL);
}

static ssize_t
ast_store_compress(struct device *dev, struct device_attribute *attr, const char *sysfsbuf, size_t count)
{
	int ret;
	unsigned long input_val;
	struct ast_video_data *ast_video = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sensor_attr = to_sensor_dev_attr_2(attr);

	ret = kstrtoul(sysfsbuf, 10, &input_val);
	if (ret)
		pr_err("%s: input invalid", __func__);
	//sensor_attr->index : ch#
	//sensor_attr->nr : attr#
	switch (sensor_attr->nr) {
	case 0:	//compress mode
		ast_set_trigger_mode(ast_video, sensor_attr->index, input_val);
		break;
	case 1: //yuv mode
		ast_set_compress_yuv_mode(ast_video, sensor_attr->index, input_val);
		break;
	case 2: //jpeg/aspeed mode
		ast_set_compress_jpeg_mode(ast_video, sensor_attr->index, input_val);
		break;
	case 3: //
		ast_set_compress_encrypt_en(ast_video, sensor_attr->index, input_val);
		break;
	case 4: //
		ast_set_compress_encrypt_key(ast_video, sensor_attr->index, (u8 *)sysfsbuf);
		break;
	case 5: //
		ast_set_compress_encrypt_mode(ast_video, sensor_attr->index);
		break;

	default:
		return -EINVAL;
	}

	return count;
}

static ssize_t
ast_show_compress(struct device *dev, struct device_attribute *attr, char *sysfsbuf)
{
	struct ast_video_data *ast_video = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sensor_attr = to_sensor_dev_attr_2(attr);

	//sensor_attr->index : ch#
	//sensor_attr->nr : attr#
	switch (sensor_attr->nr) {
	case 0:
		return sprintf(sysfsbuf, "%d [0:Single, 1:Frame, 2:Stream]\n", ast_get_trigger_mode(ast_video, sensor_attr->index));
	case 1:
		return sprintf(sysfsbuf, "%d:%s\n", ast_get_compress_yuv_mode(ast_video, sensor_attr->index), ast_get_compress_yuv_mode(ast_video, sensor_attr->index) ? "YUV420" : "YUV444");
	case 2:
		return sprintf(sysfsbuf, "%d:%s\n", ast_get_compress_jpeg_mode(ast_video, sensor_attr->index), ast_get_compress_jpeg_mode(ast_video, sensor_attr->index) ? "JPEG" : "ASPEED");
	case 3:
		return sprintf(sysfsbuf, "%d:%s\n", ast_get_compress_encrypt_en(ast_video, sensor_attr->index), ast_get_compress_encrypt_en(ast_video, sensor_attr->index) ? "Enable" : "Disable");
	case 4:
		return sprintf(sysfsbuf, "%s\n", ast_get_compress_encrypt_key(ast_video, sensor_attr->index));
	case 5:
		return sprintf(sysfsbuf, "%d:%s\n", ast_get_compress_encrypt_mode(ast_video), ast_get_compress_encrypt_mode(ast_video) ? "AES" : "RC4");
	default:
		return -EINVAL;
	}
	return -EINVAL;
}

#define sysfs_compress(index) \
static SENSOR_DEVICE_ATTR_2(compress##index##_trigger_mode, 0644, \
	ast_show_compress, ast_store_compress, 0, index); \
static SENSOR_DEVICE_ATTR_2(compress##index##_yuv, 0644, \
	ast_show_compress, ast_store_compress, 1, index); \
static SENSOR_DEVICE_ATTR_2(compress##index##_jpeg, 0644, \
	ast_show_compress, ast_store_compress, 2, index); \
static SENSOR_DEVICE_ATTR_2(compress##index##_encrypt_en, 0644, \
	ast_show_compress, ast_store_compress, 3, index); \
static SENSOR_DEVICE_ATTR_2(compress##index##_encrypt_key, 0644, \
	ast_show_compress, ast_store_compress, 4, index); \
static SENSOR_DEVICE_ATTR_2(compress##index##_encrypt_mode, 0644, \
	ast_show_compress, ast_store_compress, 5, index); \
\
static struct attribute *compress##index##_attributes[] = { \
	&sensor_dev_attr_compress##index##_trigger_mode.dev_attr.attr, \
	&sensor_dev_attr_compress##index##_yuv.dev_attr.attr, \
	&sensor_dev_attr_compress##index##_jpeg.dev_attr.attr, \
	&sensor_dev_attr_compress##index##_encrypt_en.dev_attr.attr, \
	&sensor_dev_attr_compress##index##_encrypt_key.dev_attr.attr, \
	&sensor_dev_attr_compress##index##_encrypt_mode.dev_attr.attr, \
	NULL \
}

sysfs_compress(0);
sysfs_compress(1);
/************************************************** SYS FS Capture ***********************************************************/
static const struct attribute_group compress_attribute_groups[] = {
	{ .attrs = compress0_attributes },
	{ .attrs = compress1_attributes },
};

/************************************************** SYS FS End ***********************************************************/
static const struct aspeed_video_config ast2600_config = {
	.version = 6,
	.dram_base = 0x80000000,
};

static const struct aspeed_video_config ast2500_config = {
	.version = 5,
	.dram_base = 0x80000000,
};

static const struct aspeed_video_config ast2400_config = {
	.version = 4,
	.dram_base = 0x40000000,
};

static const struct of_device_id aspeed_video_matches[] = {
	{ .compatible = "aspeed,ast2400-video",	.data = &ast2400_config, },
	{ .compatible = "aspeed,ast2500-video",	.data = &ast2500_config, },
	{ .compatible = "aspeed,ast2600-video",	.data = &ast2600_config, },
	{},
};

MODULE_DEVICE_TABLE(of, aspeed_video_matches);

#define CONFIG_AST_VIDEO_MEM_SIZE	0x2800000

static int ast_video_probe(struct platform_device *pdev)
{
	struct resource *res0;
	int ret = 0;
	int i;
	struct ast_video_data *ast_video;
	const struct of_device_id *video_dev_id;

	ast_video = devm_kzalloc(&pdev->dev, sizeof(struct ast_video_data), GFP_KERNEL);
	if (!ast_video)
		return -ENOMEM;

	video_dev_id = of_match_node(aspeed_video_matches, pdev->dev.of_node);
	if (!video_dev_id)
		return -EINVAL;

	ast_video->config = (struct aspeed_video_config *) video_dev_id->data;

	if (ast_video->config->version == 6) {
		ast_video->gfx = syscon_regmap_lookup_by_compatible("aspeed,ast2600-gfx");
		if (IS_ERR(ast_video->gfx))
			dev_err(&pdev->dev, " 2600 GFX regmap not enable\n");

		ast_video->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2600-scu");
		if (IS_ERR(ast_video->scu)) {
			dev_err(&pdev->dev, "failed to find 2600 SCU regmap\n");
			return PTR_ERR(ast_video->scu);
		}
	} else if (ast_video->config->version == 5) {
		ast_video->gfx = syscon_regmap_lookup_by_compatible("aspeed,ast2500-gfx");
		if (IS_ERR(ast_video->gfx)) {
			dev_err(&pdev->dev, "failed to find 2500 GFX regmap\n");
			return PTR_ERR(ast_video->gfx);
		}
		ast_video->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2500-scu");
		if (IS_ERR(ast_video->scu)) {
			dev_err(&pdev->dev, "failed to find 2500 SCU regmap\n");
			return PTR_ERR(ast_video->scu);
		}
	} else {
		ast_video->gfx = syscon_regmap_lookup_by_compatible("aspeed,ast-g4-gfx");
		if (IS_ERR(ast_video->gfx)) {
			dev_err(&pdev->dev, "failed to find 2400 GFX regmap\n");
			return PTR_ERR(ast_video->gfx);
		}
		ast_video->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2400-scu");
		if (IS_ERR(ast_video->scu)) {
			dev_err(&pdev->dev, "failed to find 2400 SCU regmap\n");
			return PTR_ERR(ast_video->scu);
		}
	}

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res0 == NULL) {
		dev_err(&pdev->dev, "cannot get IORESOURCE_MEM\n");
		ret = -ENOENT;
		goto out;
	}
	ast_video->reg_base = devm_ioremap_resource(&pdev->dev, res0);
	if (!ast_video->reg_base) {
		ret = -EIO;
		goto out;
	}

	//Phy assign
	ast_video->video_mem_size = CONFIG_AST_VIDEO_MEM_SIZE;
	VIDEO_DBG("video_mem_size %d MB\n", ast_video->video_mem_size / 1024 / 1024);

	of_reserved_mem_device_init(&pdev->dev);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set DMA mask\n");
		of_reserved_mem_device_release(&pdev->dev);
		goto out;
	}

	ast_video->video_mem.virt = dma_alloc_coherent(&pdev->dev, CONFIG_AST_VIDEO_MEM_SIZE, &ast_video->video_mem.dma, GFP_KERNEL);
	if (!ast_video->video_mem.virt)
		return -ENOMEM;

	ast_video->stream_phy = ast_video->video_mem.dma;
	ast_video->buff0_phy = (ast_video->video_mem.dma + 0x400000);   //4M : size 10MB
	ast_video->buff1_phy = (ast_video->video_mem.dma + 0xe00000);   //14M : size 10MB
	ast_video->bcd_phy = (ast_video->video_mem.dma + 0x1800000);    //24M : size 1MB
	ast_video->jpeg_buf0_phy = (ast_video->video_mem.dma + 0x1900000);   //25MB: size 10 MB
	ast_video->video_jpeg_offset = 0x2300000;						//TODO define
	ast_video->jpeg_phy = (ast_video->video_mem.dma + 0x2300000);   //35MB: size 4 MB
	ast_video->jpeg_tbl_phy = (ast_video->video_mem.dma + 0x2700000);       //39MB: size 1 MB

	VIDEO_DBG("\nstream_phy: %x, buff0_phy: %x, buff1_phy:%x, bcd_phy:%x\njpeg_phy:%x, jpeg_tbl_phy:%x\n",
			  (u32)ast_video->stream_phy, (u32)ast_video->buff0_phy, (u32)ast_video->buff1_phy, (u32)ast_video->bcd_phy, (u32)ast_video->jpeg_phy, (u32)ast_video->jpeg_tbl_phy);

	//virt assign
	ast_video->stream_virt = ast_video->video_mem.virt;
	ast_video->buff0_virt = ast_video->stream_virt + 0x400000; //4M : size 10MB
	ast_video->buff1_virt = ast_video->stream_virt + 0xe00000; //14M : size 10MB
	ast_video->bcd_virt = ast_video->stream_virt + 0x1800000;  //24M : size 4MB
	ast_video->jpeg_buf0_virt = ast_video->stream_virt + 0x1900000;  //25MB: size x MB
	ast_video->jpeg_virt = ast_video->stream_virt + 0x2300000; //35MB: size 4 MB
	ast_video->jpeg_tbl_virt = ast_video->stream_virt + 0x2700000;     //39MB: size 1 MB

	VIDEO_DBG("\nstream_virt: %x, buff0_virt: %x, buff1_virt:%x, bcd_virt:%x\njpeg_virt:%x, jpeg_tbl_virt:%x\n",
			  (u32)ast_video->stream_virt, (u32)ast_video->buff0_virt, (u32)ast_video->buff1_virt, (u32)ast_video->bcd_virt, (u32)ast_video->jpeg_virt, (u32)ast_video->jpeg_tbl_virt);

	memset(ast_video->stream_virt, 0, ast_video->video_mem_size);

	ast_video->irq = platform_get_irq(pdev, 0);
	if (ast_video->irq < 0) {
		dev_err(&pdev->dev, "no irq specified\n");
		ret = -ENOENT;
		goto out_region1;
	}

	ast_video->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(ast_video->reset)) {
		dev_err(&pdev->dev, "can't get video reset\n");
		return PTR_ERR(ast_video->reset);
	}

	ast_video->eclk = devm_clk_get(&pdev->dev, "eclk");
	if (IS_ERR(ast_video->eclk)) {
		dev_err(&pdev->dev, "no eclk clock defined\n");
		return PTR_ERR(ast_video->eclk);
	}

	clk_prepare_enable(ast_video->eclk);

	ast_video->vclk = devm_clk_get(&pdev->dev, "vclk");
	if (IS_ERR(ast_video->vclk)) {
		dev_err(&pdev->dev, "no vclk clock defined\n");
		return PTR_ERR(ast_video->vclk);
	}

	clk_prepare_enable(ast_video->vclk);

//	ast_scu_init_video(0);

	// default config
	ast_video->input_source = VIDEO_SOURCE_INT_VGA;
	ast_video->rc4_enable = 0;
	ast_video->multi_jpeg = 0;
	strcpy(ast_video->EncodeKeys, "fedcba9876543210");
	ast_video->scaling = 0;

	ret = misc_register(&ast_video_misc);
	if (ret) {
		pr_err("VIDEO : failed to request interrupt\n");
		goto out_region1;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &video_attribute_group);
	if (ret)
		goto out_misc;


	for (i = 0; i < 2; i++) {
		ret = sysfs_create_group(&pdev->dev.kobj, &compress_attribute_groups[i]);
		if (ret)
			goto out_create_groups;
	}

	platform_set_drvdata(pdev, ast_video);
	dev_set_drvdata(ast_video_misc.this_device, ast_video);

	ast_video_ctrl_init(ast_video);


	ret = devm_request_irq(&pdev->dev, ast_video->irq, ast_video_isr,
						   0, dev_name(&pdev->dev), ast_video);

	if (ret) {
		pr_info("VIDEO: Failed request irq %d\n", ast_video->irq);
		goto out_create_groups;
	}

	pr_info("ast_video: driver successfully loaded.\n");

	return 0;

out_create_groups:
	sysfs_remove_group(&pdev->dev.kobj, &compress_attribute_groups[0]);
	sysfs_remove_group(&pdev->dev.kobj, &video_attribute_group);

out_misc:
	misc_deregister(&ast_video_misc);

out_region1:
	if (ast_video->stream_virt)
		dma_free_coherent(&pdev->dev, CONFIG_AST_VIDEO_MEM_SIZE, ast_video->stream_virt, ast_video->video_mem.dma);
out:
	pr_warn("applesmc: driver init failed (ret=%d)!\n", ret);
	return ret;

}

static int ast_video_remove(struct platform_device *pdev)
{
	struct ast_video_data *ast_video = platform_get_drvdata(pdev);
	int i;

	VIDEO_DBG("%s\n", __func__);

	misc_deregister(&ast_video_misc);
	sysfs_remove_group(&pdev->dev.kobj, &video_attribute_group);
	for (i = 0; i < 2; i++)
		sysfs_remove_group(&pdev->dev.kobj, &compress_attribute_groups[i]);

	if (ast_video->stream_virt)
		dma_free_coherent(&pdev->dev, CONFIG_AST_VIDEO_MEM_SIZE, ast_video->stream_virt, ast_video->video_mem.dma);

	return 0;
}

#ifdef CONFIG_PM
static int
ast_video_suspend(struct platform_device *pdev, pm_message_t state)
{
	pr_info("%s: TODO\n", __func__);
	return 0;
}

static int
ast_video_resume(struct platform_device *pdev)
{
	return 0;
}

#else
#define ast_video_suspend        NULL
#define ast_video_resume         NULL
#endif

static struct platform_driver ast_video_driver = {
	.probe		= ast_video_probe,
	.remove		= ast_video_remove,
#ifdef CONFIG_PM
	.suspend        = ast_video_suspend,
	.resume         = ast_video_resume,
#endif
	.driver		= {
		.name   = KBUILD_MODNAME,
		.of_match_table = aspeed_video_matches,
	},
};

module_platform_driver(ast_video_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("AST Video Engine driver");
MODULE_LICENSE("GPL");
