/* drivers/video/msm_fb/mdp_hw.h
 *
 * Copyright (C) 2007 QUALCOMM Incorporated
 * Copyright (C) 2007 Google Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _MDP_HW_H_
#define _MDP_HW_H_

#include <mach/msm_iomap.h>
#include <linux/platform_data/video-msm_fb.h>

struct mdp_info {
	struct mdp_device mdp_dev;
	char * __iomem base;
	int irq;
};
struct mdp_blit_req;
struct mdp_device;
int mdp_ppp_blit(const struct mdp_info *mdp, struct mdp_blit_req *req,
		 struct file *src_file, unsigned long src_start,
		 unsigned long src_len, struct file *dst_file,
		 unsigned long dst_start, unsigned long dst_len);
#define mdp_writel(mdp, value, offset) writel(value, mdp->base + offset)
#define mdp_readl(mdp, offset) readl(mdp->base + offset)

#define MDP_SYNC_CONFIG_0                (0x00000)
#define MDP_SYNC_CONFIG_1                (0x00004)
#define MDP_SYNC_CONFIG_2                (0x00008)
#define MDP_SYNC_STATUS_0                (0x0000c)
#define MDP_SYNC_STATUS_1                (0x00010)
#define MDP_SYNC_STATUS_2                (0x00014)
#define MDP_SYNC_THRESH_0                (0x00018)
#define MDP_SYNC_THRESH_1                (0x0001c)
#define MDP_INTR_ENABLE                  (0x00020)
#define MDP_INTR_STATUS                  (0x00024)
#define MDP_INTR_CLEAR                   (0x00028)
#define MDP_DISPLAY0_START               (0x00030)
#define MDP_DISPLAY1_START               (0x00034)
#define MDP_DISPLAY_STATUS               (0x00038)
#define MDP_EBI2_LCD0                    (0x0003c)
#define MDP_EBI2_LCD1                    (0x00040)
#define MDP_DISPLAY0_ADDR                (0x00054)
#define MDP_DISPLAY1_ADDR                (0x00058)
#define MDP_EBI2_PORTMAP_MODE            (0x0005c)
#define MDP_MODE                         (0x00060)
#define MDP_TV_OUT_STATUS                (0x00064)
#define MDP_HW_VERSION                   (0x00070)
#define MDP_SW_RESET                     (0x00074)
#define MDP_AXI_ERROR_MASTER_STOP        (0x00078)
#define MDP_SEL_CLK_OR_HCLK_TEST_BUS     (0x0007c)
#define MDP_PRIMARY_VSYNC_OUT_CTRL       (0x00080)
#define MDP_SECONDARY_VSYNC_OUT_CTRL     (0x00084)
#define MDP_EXTERNAL_VSYNC_OUT_CTRL      (0x00088)
#define MDP_VSYNC_CTRL                   (0x0008c)
#define MDP_CGC_EN                       (0x00100)
#define MDP_CMD_STATUS                   (0x10008)
#define MDP_PROFILE_EN                   (0x10010)
#define MDP_PROFILE_COUNT                (0x10014)
#define MDP_DMA_START                    (0x10044)
#define MDP_FULL_BYPASS_WORD0            (0x10100)
#define MDP_FULL_BYPASS_WORD1            (0x10104)
#define MDP_COMMAND_CONFIG               (0x10104)
#define MDP_FULL_BYPASS_WORD2            (0x10108)
#define MDP_FULL_BYPASS_WORD3            (0x1010c)
#define MDP_FULL_BYPASS_WORD4            (0x10110)
#define MDP_FULL_BYPASS_WORD6            (0x10118)
#define MDP_FULL_BYPASS_WORD7            (0x1011c)
#define MDP_FULL_BYPASS_WORD8            (0x10120)
#define MDP_FULL_BYPASS_WORD9            (0x10124)
#define MDP_PPP_SOURCE_CONFIG            (0x10124)
#define MDP_FULL_BYPASS_WORD10           (0x10128)
#define MDP_FULL_BYPASS_WORD11           (0x1012c)
#define MDP_FULL_BYPASS_WORD12           (0x10130)
#define MDP_FULL_BYPASS_WORD13           (0x10134)
#define MDP_FULL_BYPASS_WORD14           (0x10138)
#define MDP_PPP_OPERATION_CONFIG         (0x10138)
#define MDP_FULL_BYPASS_WORD15           (0x1013c)
#define MDP_FULL_BYPASS_WORD16           (0x10140)
#define MDP_FULL_BYPASS_WORD17           (0x10144)
#define MDP_FULL_BYPASS_WORD18           (0x10148)
#define MDP_FULL_BYPASS_WORD19           (0x1014c)
#define MDP_FULL_BYPASS_WORD20           (0x10150)
#define MDP_PPP_DESTINATION_CONFIG       (0x10150)
#define MDP_FULL_BYPASS_WORD21           (0x10154)
#define MDP_FULL_BYPASS_WORD22           (0x10158)
#define MDP_FULL_BYPASS_WORD23           (0x1015c)
#define MDP_FULL_BYPASS_WORD24           (0x10160)
#define MDP_FULL_BYPASS_WORD25           (0x10164)
#define MDP_FULL_BYPASS_WORD26           (0x10168)
#define MDP_FULL_BYPASS_WORD27           (0x1016c)
#define MDP_FULL_BYPASS_WORD29           (0x10174)
#define MDP_FULL_BYPASS_WORD30           (0x10178)
#define MDP_FULL_BYPASS_WORD31           (0x1017c)
#define MDP_FULL_BYPASS_WORD32           (0x10180)
#define MDP_DMA_CONFIG                   (0x10180)
#define MDP_FULL_BYPASS_WORD33           (0x10184)
#define MDP_FULL_BYPASS_WORD34           (0x10188)
#define MDP_FULL_BYPASS_WORD35           (0x1018c)
#define MDP_FULL_BYPASS_WORD37           (0x10194)
#define MDP_FULL_BYPASS_WORD39           (0x1019c)
#define MDP_FULL_BYPASS_WORD40           (0x101a0)
#define MDP_FULL_BYPASS_WORD41           (0x101a4)
#define MDP_FULL_BYPASS_WORD43           (0x101ac)
#define MDP_FULL_BYPASS_WORD46           (0x101b8)
#define MDP_FULL_BYPASS_WORD47           (0x101bc)
#define MDP_FULL_BYPASS_WORD48           (0x101c0)
#define MDP_FULL_BYPASS_WORD49           (0x101c4)
#define MDP_FULL_BYPASS_WORD50           (0x101c8)
#define MDP_FULL_BYPASS_WORD51           (0x101cc)
#define MDP_FULL_BYPASS_WORD52           (0x101d0)
#define MDP_FULL_BYPASS_WORD53           (0x101d4)
#define MDP_FULL_BYPASS_WORD54           (0x101d8)
#define MDP_FULL_BYPASS_WORD55           (0x101dc)
#define MDP_FULL_BYPASS_WORD56           (0x101e0)
#define MDP_FULL_BYPASS_WORD57           (0x101e4)
#define MDP_FULL_BYPASS_WORD58           (0x101e8)
#define MDP_FULL_BYPASS_WORD59           (0x101ec)
#define MDP_FULL_BYPASS_WORD60           (0x101f0)
#define MDP_VSYNC_THRESHOLD              (0x101f0)
#define MDP_FULL_BYPASS_WORD61           (0x101f4)
#define MDP_FULL_BYPASS_WORD62           (0x101f8)
#define MDP_FULL_BYPASS_WORD63           (0x101fc)
#define MDP_TFETCH_TEST_MODE             (0x20004)
#define MDP_TFETCH_STATUS                (0x20008)
#define MDP_TFETCH_TILE_COUNT            (0x20010)
#define MDP_TFETCH_FETCH_COUNT           (0x20014)
#define MDP_TFETCH_CONSTANT_COLOR        (0x20040)
#define MDP_CSC_BYPASS                   (0x40004)
#define MDP_SCALE_COEFF_LSB              (0x5fffc)
#define MDP_TV_OUT_CTL                   (0xc0000)
#define MDP_TV_OUT_FIR_COEFF             (0xc0004)
#define MDP_TV_OUT_BUF_ADDR              (0xc0008)
#define MDP_TV_OUT_CC_DATA               (0xc000c)
#define MDP_TV_OUT_SOBEL                 (0xc0010)
#define MDP_TV_OUT_Y_CLAMP               (0xc0018)
#define MDP_TV_OUT_CB_CLAMP              (0xc001c)
#define MDP_TV_OUT_CR_CLAMP              (0xc0020)
#define MDP_TEST_MODE_CLK                (0xd0000)
#define MDP_TEST_MISR_RESET_CLK          (0xd0004)
#define MDP_TEST_EXPORT_MISR_CLK         (0xd0008)
#define MDP_TEST_MISR_CURR_VAL_CLK       (0xd000c)
#define MDP_TEST_MODE_HCLK               (0xd0100)
#define MDP_TEST_MISR_RESET_HCLK         (0xd0104)
#define MDP_TEST_EXPORT_MISR_HCLK        (0xd0108)
#define MDP_TEST_MISR_CURR_VAL_HCLK      (0xd010c)
#define MDP_TEST_MODE_DCLK               (0xd0200)
#define MDP_TEST_MISR_RESET_DCLK         (0xd0204)
#define MDP_TEST_EXPORT_MISR_DCLK        (0xd0208)
#define MDP_TEST_MISR_CURR_VAL_DCLK      (0xd020c)
#define MDP_TEST_CAPTURED_DCLK           (0xd0210)
#define MDP_TEST_MISR_CAPT_VAL_DCLK      (0xd0214)
#define MDP_LCDC_CTL                     (0xe0000)
#define MDP_LCDC_HSYNC_CTL               (0xe0004)
#define MDP_LCDC_VSYNC_CTL               (0xe0008)
#define MDP_LCDC_ACTIVE_HCTL             (0xe000c)
#define MDP_LCDC_ACTIVE_VCTL             (0xe0010)
#define MDP_LCDC_BORDER_CLR              (0xe0014)
#define MDP_LCDC_H_BLANK                 (0xe0018)
#define MDP_LCDC_V_BLANK                 (0xe001c)
#define MDP_LCDC_UNDERFLOW_CLR           (0xe0020)
#define MDP_LCDC_HSYNC_SKEW              (0xe0024)
#define MDP_LCDC_TEST_CTL                (0xe0028)
#define MDP_LCDC_LINE_IRQ                (0xe002c)
#define MDP_LCDC_CTL_POLARITY            (0xe0030)
#define MDP_LCDC_DMA_CONFIG              (0xe1000)
#define MDP_LCDC_DMA_SIZE                (0xe1004)
#define MDP_LCDC_DMA_IBUF_ADDR           (0xe1008)
#define MDP_LCDC_DMA_IBUF_Y_STRIDE       (0xe100c)


#define MDP_DMA2_TERM 0x1
#define MDP_DMA3_TERM 0x2
#define MDP_PPP_TERM 0x3

/* MDP_INTR_ENABLE */
#define DL0_ROI_DONE           (1<<0)
#define DL1_ROI_DONE           (1<<1)
#define DL0_DMA2_TERM_DONE     (1<<2)
#define DL1_DMA2_TERM_DONE     (1<<3)
#define DL0_PPP_TERM_DONE      (1<<4)
#define DL1_PPP_TERM_DONE      (1<<5)
#define TV_OUT_DMA3_DONE       (1<<6)
#define TV_ENC_UNDERRUN        (1<<7)
#define DL0_FETCH_DONE         (1<<11)
#define DL1_FETCH_DONE         (1<<12)

#define MDP_PPP_BUSY_STATUS (DL0_ROI_DONE| \
			   DL1_ROI_DONE| \
			   DL0_PPP_TERM_DONE| \
			   DL1_PPP_TERM_DONE)

#define MDP_ANY_INTR_MASK (DL0_ROI_DONE| \
			   DL1_ROI_DONE| \
			   DL0_DMA2_TERM_DONE| \
			   DL1_DMA2_TERM_DONE| \
			   DL0_PPP_TERM_DONE| \
			   DL1_PPP_TERM_DONE| \
			   DL0_FETCH_DONE| \
			   DL1_FETCH_DONE| \
			   TV_ENC_UNDERRUN)

#define MDP_TOP_LUMA       16
#define MDP_TOP_CHROMA     0
#define MDP_BOTTOM_LUMA    19
#define MDP_BOTTOM_CHROMA  3
#define MDP_LEFT_LUMA      22
#define MDP_LEFT_CHROMA    6
#define MDP_RIGHT_LUMA     25
#define MDP_RIGHT_CHROMA   9

#define CLR_G 0x0
#define CLR_B 0x1
#define CLR_R 0x2
#define CLR_ALPHA 0x3

#define CLR_Y  CLR_G
#define CLR_CB CLR_B
#define CLR_CR CLR_R

/* from lsb to msb */
#define MDP_GET_PACK_PATTERN(a, x, y, z, bit) \
	(((a)<<(bit*3))|((x)<<(bit*2))|((y)<<bit)|(z))

/* MDP_SYNC_CONFIG_0/1/2 */
#define MDP_SYNCFG_HGT_LOC 22
#define MDP_SYNCFG_VSYNC_EXT_EN (1<<21)
#define MDP_SYNCFG_VSYNC_INT_EN (1<<20)

/* MDP_SYNC_THRESH_0 */
#define MDP_PRIM_BELOW_LOC 0
#define MDP_PRIM_ABOVE_LOC 8

/* MDP_{PRIMARY,SECONDARY,EXTERNAL}_VSYNC_OUT_CRL */
#define VSYNC_PULSE_EN (1<<31)
#define VSYNC_PULSE_INV (1<<30)

/* MDP_VSYNC_CTRL */
#define DISP0_VSYNC_MAP_VSYNC0 0
#define DISP0_VSYNC_MAP_VSYNC1 (1<<0)
#define DISP0_VSYNC_MAP_VSYNC2 ((1<<0)|(1<<1))

#define DISP1_VSYNC_MAP_VSYNC0 0
#define DISP1_VSYNC_MAP_VSYNC1 (1<<2)
#define DISP1_VSYNC_MAP_VSYNC2 ((1<<2)|(1<<3))

#define PRIMARY_LCD_SYNC_EN (1<<4)
#define PRIMARY_LCD_SYNC_DISABLE 0

#define SECONDARY_LCD_SYNC_EN (1<<5)
#define SECONDARY_LCD_SYNC_DISABLE 0

#define EXTERNAL_LCD_SYNC_EN (1<<6)
#define EXTERNAL_LCD_SYNC_DISABLE 0

/* MDP_VSYNC_THRESHOLD / MDP_FULL_BYPASS_WORD60 */
#define VSYNC_THRESHOLD_ABOVE_LOC 0
#define VSYNC_THRESHOLD_BELOW_LOC 16
#define VSYNC_ANTI_TEAR_EN (1<<31)

/* MDP_COMMAND_CONFIG / MDP_FULL_BYPASS_WORD1 */
#define MDP_CMD_DBGBUS_EN (1<<0)

/* MDP_PPP_SOURCE_CONFIG / MDP_FULL_BYPASS_WORD9&53 */
#define PPP_SRC_C0G_8BIT ((1<<1)|(1<<0))
#define PPP_SRC_C1B_8BIT ((1<<3)|(1<<2))
#define PPP_SRC_C2R_8BIT ((1<<5)|(1<<4))
#define PPP_SRC_C3A_8BIT ((1<<7)|(1<<6))

#define PPP_SRC_C0G_6BIT (1<<1)
#define PPP_SRC_C1B_6BIT (1<<3)
#define PPP_SRC_C2R_6BIT (1<<5)

#define PPP_SRC_C0G_5BIT (1<<0)
#define PPP_SRC_C1B_5BIT (1<<2)
#define PPP_SRC_C2R_5BIT (1<<4)

#define PPP_SRC_C3ALPHA_EN (1<<8)

#define PPP_SRC_BPP_1BYTES 0
#define PPP_SRC_BPP_2BYTES (1<<9)
#define PPP_SRC_BPP_3BYTES (1<<10)
#define PPP_SRC_BPP_4BYTES ((1<<10)|(1<<9))

#define PPP_SRC_BPP_ROI_ODD_X (1<<11)
#define PPP_SRC_BPP_ROI_ODD_Y (1<<12)
#define PPP_SRC_INTERLVD_2COMPONENTS (1<<13)
#define PPP_SRC_INTERLVD_3COMPONENTS (1<<14)
#define PPP_SRC_INTERLVD_4COMPONENTS ((1<<14)|(1<<13))


/* RGB666 unpack format
** TIGHT means R6+G6+B6 together
** LOOSE means R6+2 +G6+2+ B6+2 (with MSB)
**          or 2+R6 +2+G6 +2+B6 (with LSB)
*/
#define PPP_SRC_PACK_TIGHT (1<<17)
#define PPP_SRC_PACK_LOOSE 0
#define PPP_SRC_PACK_ALIGN_LSB 0
#define PPP_SRC_PACK_ALIGN_MSB (1<<18)

#define PPP_SRC_PLANE_INTERLVD 0
#define PPP_SRC_PLANE_PSEUDOPLNR (1<<20)

#define PPP_SRC_WMV9_MODE (1<<21)

/* MDP_PPP_OPERATION_CONFIG / MDP_FULL_BYPASS_WORD14 */
#define PPP_OP_SCALE_X_ON (1<<0)
#define PPP_OP_SCALE_Y_ON (1<<1)

#define PPP_OP_CONVERT_RGB2YCBCR 0
#define PPP_OP_CONVERT_YCBCR2RGB (1<<2)
#define PPP_OP_CONVERT_ON (1<<3)

#define PPP_OP_CONVERT_MATRIX_PRIMARY 0
#define PPP_OP_CONVERT_MATRIX_SECONDARY (1<<4)

#define PPP_OP_LUT_C0_ON (1<<5)
#define PPP_OP_LUT_C1_ON (1<<6)
#define PPP_OP_LUT_C2_ON (1<<7)

/* rotate or blend enable */
#define PPP_OP_ROT_ON (1<<8)

#define PPP_OP_ROT_90 (1<<9)
#define PPP_OP_FLIP_LR (1<<10)
#define PPP_OP_FLIP_UD (1<<11)

#define PPP_OP_BLEND_ON (1<<12)

#define PPP_OP_BLEND_SRCPIXEL_ALPHA 0
#define PPP_OP_BLEND_DSTPIXEL_ALPHA (1<<13)
#define PPP_OP_BLEND_CONSTANT_ALPHA (1<<14)
#define PPP_OP_BLEND_SRCPIXEL_TRANSP ((1<<13)|(1<<14))

#define PPP_OP_BLEND_ALPHA_BLEND_NORMAL 0
#define PPP_OP_BLEND_ALPHA_BLEND_REVERSE (1<<15)

#define PPP_OP_DITHER_EN (1<<16)

#define PPP_OP_COLOR_SPACE_RGB 0
#define PPP_OP_COLOR_SPACE_YCBCR (1<<17)

#define PPP_OP_SRC_CHROMA_RGB 0
#define PPP_OP_SRC_CHROMA_H2V1 (1<<18)
#define PPP_OP_SRC_CHROMA_H1V2 (1<<19)
#define PPP_OP_SRC_CHROMA_420 ((1<<18)|(1<<19))
#define PPP_OP_SRC_CHROMA_COSITE 0
#define PPP_OP_SRC_CHROMA_OFFSITE (1<<20)

#define PPP_OP_DST_CHROMA_RGB 0
#define PPP_OP_DST_CHROMA_H2V1 (1<<21)
#define PPP_OP_DST_CHROMA_H1V2 (1<<22)
#define PPP_OP_DST_CHROMA_420 ((1<<21)|(1<<22))
#define PPP_OP_DST_CHROMA_COSITE 0
#define PPP_OP_DST_CHROMA_OFFSITE (1<<23)

#define PPP_BLEND_ALPHA_TRANSP (1<<24)

#define PPP_OP_BG_CHROMA_RGB 0
#define PPP_OP_BG_CHROMA_H2V1 (1<<25)
#define PPP_OP_BG_CHROMA_H1V2 (1<<26)
#define PPP_OP_BG_CHROMA_420 ((1<<25)|(1<<26))
#define PPP_OP_BG_CHROMA_SITE_COSITE 0
#define PPP_OP_BG_CHROMA_SITE_OFFSITE (1<<27)

/* MDP_PPP_DESTINATION_CONFIG / MDP_FULL_BYPASS_WORD20 */
#define PPP_DST_C0G_8BIT ((1<<0)|(1<<1))
#define PPP_DST_C1B_8BIT ((1<<3)|(1<<2))
#define PPP_DST_C2R_8BIT ((1<<5)|(1<<4))
#define PPP_DST_C3A_8BIT ((1<<7)|(1<<6))

#define PPP_DST_C0G_6BIT (1<<1)
#define PPP_DST_C1B_6BIT (1<<3)
#define PPP_DST_C2R_6BIT (1<<5)

#define PPP_DST_C0G_5BIT (1<<0)
#define PPP_DST_C1B_5BIT (1<<2)
#define PPP_DST_C2R_5BIT (1<<4)

#define PPP_DST_C3A_8BIT ((1<<7)|(1<<6))
#define PPP_DST_C3ALPHA_EN (1<<8)

#define PPP_DST_INTERLVD_2COMPONENTS (1<<9)
#define PPP_DST_INTERLVD_3COMPONENTS (1<<10)
#define PPP_DST_INTERLVD_4COMPONENTS ((1<<10)|(1<<9))
#define PPP_DST_INTERLVD_6COMPONENTS ((1<<11)|(1<<9))

#define PPP_DST_PACK_LOOSE 0
#define PPP_DST_PACK_TIGHT (1<<13)
#define PPP_DST_PACK_ALIGN_LSB 0
#define PPP_DST_PACK_ALIGN_MSB (1<<14)

#define PPP_DST_OUT_SEL_AXI 0
#define PPP_DST_OUT_SEL_MDDI (1<<15)

#define PPP_DST_BPP_2BYTES (1<<16)
#define PPP_DST_BPP_3BYTES (1<<17)
#define PPP_DST_BPP_4BYTES ((1<<17)|(1<<16))

#define PPP_DST_PLANE_INTERLVD 0
#define PPP_DST_PLANE_PLANAR (1<<18)
#define PPP_DST_PLANE_PSEUDOPLNR (1<<19)

#define PPP_DST_TO_TV (1<<20)

#define PPP_DST_MDDI_PRIMARY 0
#define PPP_DST_MDDI_SECONDARY (1<<21)
#define PPP_DST_MDDI_EXTERNAL (1<<22)

/* image configurations by image type */
#define PPP_CFG_MDP_RGB_565(dir)       (PPP_##dir##_C2R_5BIT | \
					PPP_##dir##_C0G_6BIT | \
					PPP_##dir##_C1B_5BIT | \
					PPP_##dir##_BPP_2BYTES | \
					PPP_##dir##_INTERLVD_3COMPONENTS | \
					PPP_##dir##_PACK_TIGHT | \
					PPP_##dir##_PACK_ALIGN_LSB | \
					PPP_##dir##_PLANE_INTERLVD)

#define PPP_CFG_MDP_RGB_888(dir)       (PPP_##dir##_C2R_8BIT | \
					PPP_##dir##_C0G_8BIT | \
					PPP_##dir##_C1B_8BIT | \
					PPP_##dir##_BPP_3BYTES | \
					PPP_##dir##_INTERLVD_3COMPONENTS | \
					PPP_##dir##_PACK_TIGHT | \
					PPP_##dir##_PACK_ALIGN_LSB | \
					PPP_##dir##_PLANE_INTERLVD)

#define PPP_CFG_MDP_ARGB_8888(dir)     (PPP_##dir##_C2R_8BIT | \
					PPP_##dir##_C0G_8BIT | \
					PPP_##dir##_C1B_8BIT | \
					PPP_##dir##_C3A_8BIT | \
					PPP_##dir##_C3ALPHA_EN | \
					PPP_##dir##_BPP_4BYTES | \
					PPP_##dir##_INTERLVD_4COMPONENTS | \
					PPP_##dir##_PACK_TIGHT | \
					PPP_##dir##_PACK_ALIGN_LSB | \
					PPP_##dir##_PLANE_INTERLVD)

#define PPP_CFG_MDP_XRGB_8888(dir) PPP_CFG_MDP_ARGB_8888(dir)
#define PPP_CFG_MDP_RGBA_8888(dir) PPP_CFG_MDP_ARGB_8888(dir)
#define PPP_CFG_MDP_BGRA_8888(dir) PPP_CFG_MDP_ARGB_8888(dir)
#define PPP_CFG_MDP_RGBX_8888(dir) PPP_CFG_MDP_ARGB_8888(dir)

#define PPP_CFG_MDP_Y_CBCR_H2V2(dir)   (PPP_##dir##_C2R_8BIT | \
					PPP_##dir##_C0G_8BIT | \
					PPP_##dir##_C1B_8BIT | \
					PPP_##dir##_C3A_8BIT | \
					PPP_##dir##_BPP_2BYTES | \
					PPP_##dir##_INTERLVD_2COMPONENTS | \
					PPP_##dir##_PACK_TIGHT | \
					PPP_##dir##_PACK_ALIGN_LSB | \
					PPP_##dir##_PLANE_PSEUDOPLNR)

#define PPP_CFG_MDP_Y_CRCB_H2V2(dir)	PPP_CFG_MDP_Y_CBCR_H2V2(dir)

#define PPP_CFG_MDP_YCRYCB_H2V1(dir)   (PPP_##dir##_C2R_8BIT | \
					PPP_##dir##_C0G_8BIT | \
					PPP_##dir##_C1B_8BIT | \
					PPP_##dir##_C3A_8BIT | \
					PPP_##dir##_BPP_2BYTES | \
					PPP_##dir##_INTERLVD_4COMPONENTS | \
					PPP_##dir##_PACK_TIGHT | \
					PPP_##dir##_PACK_ALIGN_LSB |\
					PPP_##dir##_PLANE_INTERLVD)

#define PPP_CFG_MDP_Y_CBCR_H2V1(dir)   (PPP_##dir##_C2R_8BIT | \
					PPP_##dir##_C0G_8BIT | \
					PPP_##dir##_C1B_8BIT | \
					PPP_##dir##_C3A_8BIT | \
					PPP_##dir##_BPP_2BYTES |   \
					PPP_##dir##_INTERLVD_2COMPONENTS |  \
					PPP_##dir##_PACK_TIGHT | \
					PPP_##dir##_PACK_ALIGN_LSB | \
					PPP_##dir##_PLANE_PSEUDOPLNR)

#define PPP_CFG_MDP_Y_CRCB_H2V1(dir)	PPP_CFG_MDP_Y_CBCR_H2V1(dir)

#define PPP_PACK_PATTERN_MDP_RGB_565 \
	MDP_GET_PACK_PATTERN(0, CLR_R, CLR_G, CLR_B, 8)
#define PPP_PACK_PATTERN_MDP_RGB_888 PPP_PACK_PATTERN_MDP_RGB_565
#define PPP_PACK_PATTERN_MDP_XRGB_8888 \
	MDP_GET_PACK_PATTERN(CLR_B, CLR_G, CLR_R, CLR_ALPHA, 8)
#define PPP_PACK_PATTERN_MDP_ARGB_8888 PPP_PACK_PATTERN_MDP_XRGB_8888
#define PPP_PACK_PATTERN_MDP_RGBA_8888 \
	MDP_GET_PACK_PATTERN(CLR_ALPHA, CLR_B, CLR_G, CLR_R, 8)
#define PPP_PACK_PATTERN_MDP_BGRA_8888 \
	MDP_GET_PACK_PATTERN(CLR_ALPHA, CLR_R, CLR_G, CLR_B, 8)
#define PPP_PACK_PATTERN_MDP_RGBX_8888 \
	MDP_GET_PACK_PATTERN(CLR_ALPHA, CLR_B, CLR_G, CLR_R, 8)
#define PPP_PACK_PATTERN_MDP_Y_CBCR_H2V1 \
	MDP_GET_PACK_PATTERN(0, 0, CLR_CB, CLR_CR, 8)
#define PPP_PACK_PATTERN_MDP_Y_CBCR_H2V2 PPP_PACK_PATTERN_MDP_Y_CBCR_H2V1
#define PPP_PACK_PATTERN_MDP_Y_CRCB_H2V1 \
	MDP_GET_PACK_PATTERN(0, 0, CLR_CR, CLR_CB, 8)
#define PPP_PACK_PATTERN_MDP_Y_CRCB_H2V2 PPP_PACK_PATTERN_MDP_Y_CRCB_H2V1
#define PPP_PACK_PATTERN_MDP_YCRYCB_H2V1 \
	MDP_GET_PACK_PATTERN(CLR_Y, CLR_R, CLR_Y, CLR_B, 8)

#define PPP_CHROMA_SAMP_MDP_RGB_565(dir) PPP_OP_##dir##_CHROMA_RGB
#define PPP_CHROMA_SAMP_MDP_RGB_888(dir) PPP_OP_##dir##_CHROMA_RGB
#define PPP_CHROMA_SAMP_MDP_XRGB_8888(dir) PPP_OP_##dir##_CHROMA_RGB
#define PPP_CHROMA_SAMP_MDP_ARGB_8888(dir) PPP_OP_##dir##_CHROMA_RGB
#define PPP_CHROMA_SAMP_MDP_RGBA_8888(dir) PPP_OP_##dir##_CHROMA_RGB
#define PPP_CHROMA_SAMP_MDP_BGRA_8888(dir) PPP_OP_##dir##_CHROMA_RGB
#define PPP_CHROMA_SAMP_MDP_RGBX_8888(dir) PPP_OP_##dir##_CHROMA_RGB
#define PPP_CHROMA_SAMP_MDP_Y_CBCR_H2V1(dir) PPP_OP_##dir##_CHROMA_H2V1
#define PPP_CHROMA_SAMP_MDP_Y_CBCR_H2V2(dir) PPP_OP_##dir##_CHROMA_420
#define PPP_CHROMA_SAMP_MDP_Y_CRCB_H2V1(dir) PPP_OP_##dir##_CHROMA_H2V1
#define PPP_CHROMA_SAMP_MDP_Y_CRCB_H2V2(dir) PPP_OP_##dir##_CHROMA_420
#define PPP_CHROMA_SAMP_MDP_YCRYCB_H2V1(dir) PPP_OP_##dir##_CHROMA_H2V1

/* Helpful array generation macros */
#define PPP_ARRAY0(name) \
	[MDP_RGB_565] = PPP_##name##_MDP_RGB_565,\
	[MDP_RGB_888] = PPP_##name##_MDP_RGB_888,\
	[MDP_XRGB_8888] = PPP_##name##_MDP_XRGB_8888,\
	[MDP_ARGB_8888] = PPP_##name##_MDP_ARGB_8888,\
	[MDP_RGBA_8888] = PPP_##name##_MDP_RGBA_8888,\
	[MDP_BGRA_8888] = PPP_##name##_MDP_BGRA_8888,\
	[MDP_RGBX_8888] = PPP_##name##_MDP_RGBX_8888,\
	[MDP_Y_CBCR_H2V1] = PPP_##name##_MDP_Y_CBCR_H2V1,\
	[MDP_Y_CBCR_H2V2] = PPP_##name##_MDP_Y_CBCR_H2V2,\
	[MDP_Y_CRCB_H2V1] = PPP_##name##_MDP_Y_CRCB_H2V1,\
	[MDP_Y_CRCB_H2V2] = PPP_##name##_MDP_Y_CRCB_H2V2,\
	[MDP_YCRYCB_H2V1] = PPP_##name##_MDP_YCRYCB_H2V1

#define PPP_ARRAY1(name, dir) \
	[MDP_RGB_565] = PPP_##name##_MDP_RGB_565(dir),\
	[MDP_RGB_888] = PPP_##name##_MDP_RGB_888(dir),\
	[MDP_XRGB_8888] = PPP_##name##_MDP_XRGB_8888(dir),\
	[MDP_ARGB_8888] = PPP_##name##_MDP_ARGB_8888(dir),\
	[MDP_RGBA_8888] = PPP_##name##_MDP_RGBA_8888(dir),\
	[MDP_BGRA_8888] = PPP_##name##_MDP_BGRA_8888(dir),\
	[MDP_RGBX_8888] = PPP_##name##_MDP_RGBX_8888(dir),\
	[MDP_Y_CBCR_H2V1] = PPP_##name##_MDP_Y_CBCR_H2V1(dir),\
	[MDP_Y_CBCR_H2V2] = PPP_##name##_MDP_Y_CBCR_H2V2(dir),\
	[MDP_Y_CRCB_H2V1] = PPP_##name##_MDP_Y_CRCB_H2V1(dir),\
	[MDP_Y_CRCB_H2V2] = PPP_##name##_MDP_Y_CRCB_H2V2(dir),\
	[MDP_YCRYCB_H2V1] = PPP_##name##_MDP_YCRYCB_H2V1(dir)

#define IS_YCRCB(img) ((img == MDP_Y_CRCB_H2V2) | (img == MDP_Y_CBCR_H2V2) | \
		       (img == MDP_Y_CRCB_H2V1) | (img == MDP_Y_CBCR_H2V1) | \
		       (img == MDP_YCRYCB_H2V1))
#define IS_RGB(img) ((img == MDP_RGB_565) | (img == MDP_RGB_888) | \
		     (img == MDP_ARGB_8888) | (img == MDP_RGBA_8888) | \
		     (img == MDP_XRGB_8888) | (img == MDP_BGRA_8888) | \
		     (img == MDP_RGBX_8888))
#define HAS_ALPHA(img) ((img == MDP_ARGB_8888) | (img == MDP_RGBA_8888) | \
			(img == MDP_BGRA_8888))

#define IS_PSEUDOPLNR(img) ((img == MDP_Y_CRCB_H2V2) | \
			    (img == MDP_Y_CBCR_H2V2) | \
			    (img == MDP_Y_CRCB_H2V1) | \
			    (img == MDP_Y_CBCR_H2V1))

/* Mappings from addr to purpose */
#define PPP_ADDR_SRC_ROI		MDP_FULL_BYPASS_WORD2
#define PPP_ADDR_SRC0			MDP_FULL_BYPASS_WORD3
#define PPP_ADDR_SRC1			MDP_FULL_BYPASS_WORD4
#define PPP_ADDR_SRC_YSTRIDE		MDP_FULL_BYPASS_WORD7
#define PPP_ADDR_SRC_CFG		MDP_FULL_BYPASS_WORD9
#define PPP_ADDR_SRC_PACK_PATTERN	MDP_FULL_BYPASS_WORD10
#define PPP_ADDR_OPERATION		MDP_FULL_BYPASS_WORD14
#define PPP_ADDR_PHASEX_INIT		MDP_FULL_BYPASS_WORD15
#define PPP_ADDR_PHASEY_INIT		MDP_FULL_BYPASS_WORD16
#define PPP_ADDR_PHASEX_STEP		MDP_FULL_BYPASS_WORD17
#define PPP_ADDR_PHASEY_STEP		MDP_FULL_BYPASS_WORD18
#define PPP_ADDR_ALPHA_TRANSP		MDP_FULL_BYPASS_WORD19
#define PPP_ADDR_DST_CFG		MDP_FULL_BYPASS_WORD20
#define PPP_ADDR_DST_PACK_PATTERN	MDP_FULL_BYPASS_WORD21
#define PPP_ADDR_DST_ROI		MDP_FULL_BYPASS_WORD25
#define PPP_ADDR_DST0			MDP_FULL_BYPASS_WORD26
#define PPP_ADDR_DST1			MDP_FULL_BYPASS_WORD27
#define PPP_ADDR_DST_YSTRIDE		MDP_FULL_BYPASS_WORD30
#define PPP_ADDR_EDGE			MDP_FULL_BYPASS_WORD46
#define PPP_ADDR_BG0			MDP_FULL_BYPASS_WORD48
#define PPP_ADDR_BG1			MDP_FULL_BYPASS_WORD49
#define PPP_ADDR_BG_YSTRIDE		MDP_FULL_BYPASS_WORD51
#define PPP_ADDR_BG_CFG			MDP_FULL_BYPASS_WORD53
#define PPP_ADDR_BG_PACK_PATTERN	MDP_FULL_BYPASS_WORD54

/* MDP_DMA_CONFIG / MDP_FULL_BYPASS_WORD32 */
#define DMA_DSTC0G_6BITS (1<<1)
#define DMA_DSTC1B_6BITS (1<<3)
#define DMA_DSTC2R_6BITS (1<<5)
#define DMA_DSTC0G_5BITS (1<<0)
#define DMA_DSTC1B_5BITS (1<<2)
#define DMA_DSTC2R_5BITS (1<<4)

#define DMA_PACK_TIGHT (1<<6)
#define DMA_PACK_LOOSE 0
#define DMA_PACK_ALIGN_LSB 0
#define DMA_PACK_ALIGN_MSB (1<<7)
#define DMA_PACK_PATTERN_RGB \
	(MDP_GET_PACK_PATTERN(0, CLR_R, CLR_G, CLR_B, 2)<<8)

#define DMA_OUT_SEL_AHB  0
#define DMA_OUT_SEL_MDDI (1<<14)
#define DMA_AHBM_LCD_SEL_PRIMARY 0
#define DMA_AHBM_LCD_SEL_SECONDARY (1<<15)
#define DMA_IBUF_C3ALPHA_EN (1<<16)
#define DMA_DITHER_EN (1<<17)

#define DMA_MDDI_DMAOUT_LCD_SEL_PRIMARY 0
#define DMA_MDDI_DMAOUT_LCD_SEL_SECONDARY (1<<18)
#define DMA_MDDI_DMAOUT_LCD_SEL_EXTERNAL (1<<19)

#define DMA_IBUF_FORMAT_RGB565 (1<<20)
#define DMA_IBUF_FORMAT_RGB888_OR_ARGB8888 0

#define DMA_IBUF_NONCONTIGUOUS (1<<21)

/* MDDI REGISTER ? */
#define MDDI_VDO_PACKET_DESC  0x5666
#define MDDI_VDO_PACKET_PRIM  0xC3
#define MDDI_VDO_PACKET_SECD  0xC0

#endif
