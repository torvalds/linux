/*
 * PKUnity UNIGFX Registers
 */

#define UDE_BASE      (PKUNITY_UNIGFX_BASE + 0x1400)
#define UGE_BASE      (PKUNITY_UNIGFX_BASE + 0x0000)

/*
 * command reg for UNIGFX DE
 */
/*
 * control reg UDE_CFG
 */
#define UDE_CFG       __REG(UDE_BASE + 0x0000)
/*
 * framebuffer start address reg UDE_FSA
 */
#define UDE_FSA       __REG(UDE_BASE + 0x0004)
/*
 * line size reg UDE_LS
 */
#define UDE_LS        __REG(UDE_BASE + 0x0008)
/*
 * pitch size reg UDE_PS
 */
#define UDE_PS        __REG(UDE_BASE + 0x000C)
/*
 * horizontal active time reg UDE_HAT
 */
#define UDE_HAT       __REG(UDE_BASE + 0x0010)
/*
 * horizontal blank time reg UDE_HBT
 */
#define UDE_HBT       __REG(UDE_BASE + 0x0014)
/*
 * horizontal sync time reg UDE_HST
 */
#define UDE_HST       __REG(UDE_BASE + 0x0018)
/*
 * vertival active time reg UDE_VAT
 */
#define UDE_VAT       __REG(UDE_BASE + 0x001C)
/*
 * vertival blank time reg UDE_VBT
 */
#define UDE_VBT       __REG(UDE_BASE + 0x0020)
/*
 * vertival sync time reg UDE_VST
 */
#define UDE_VST       __REG(UDE_BASE + 0x0024)
/*
 * cursor position UDE_CXY
 */
#define UDE_CXY       __REG(UDE_BASE + 0x0028)
/*
 * cursor front color UDE_CC0
 */
#define UDE_CC0       __REG(UDE_BASE + 0x002C)
/*
 * cursor background color UDE_CC1
 */
#define UDE_CC1       __REG(UDE_BASE + 0x0030)
/*
 * video position UDE_VXY
 */
#define UDE_VXY       __REG(UDE_BASE + 0x0034)
/*
 * video start address reg UDE_VSA
 */
#define UDE_VSA       __REG(UDE_BASE + 0x0040)
/*
 * video size reg UDE_VS
 */
#define UDE_VS        __REG(UDE_BASE + 0x004C)

/*
 * command reg for UNIGFX GE
 */
/*
 * src xy reg UGE_SRCXY
 */
#define UGE_SRCXY     __REG(UGE_BASE + 0x0000)
/*
 * dst xy reg UGE_DSTXY
 */
#define UGE_DSTXY     __REG(UGE_BASE + 0x0004)
/*
 * pitch reg UGE_PITCH
 */
#define UGE_PITCH     __REG(UGE_BASE + 0x0008)
/*
 * src start reg UGE_SRCSTART
 */
#define UGE_SRCSTART  __REG(UGE_BASE + 0x000C)
/*
 * dst start reg UGE_DSTSTART
 */
#define UGE_DSTSTART  __REG(UGE_BASE + 0x0010)
/*
 * width height reg UGE_WIDHEIGHT
 */
#define UGE_WIDHEIGHT __REG(UGE_BASE + 0x0014)
/*
 * rop alpah reg UGE_ROPALPHA
 */
#define UGE_ROPALPHA  __REG(UGE_BASE + 0x0018)
/*
 * front color UGE_FCOLOR
 */
#define UGE_FCOLOR    __REG(UGE_BASE + 0x001C)
/*
 * background color UGE_BCOLOR
 */
#define UGE_BCOLOR    __REG(UGE_BASE + 0x0020)
/*
 * src color key for high value UGE_SCH
 */
#define UGE_SCH       __REG(UGE_BASE + 0x0024)
/*
 * dst color key for high value UGE_DCH
 */
#define UGE_DCH       __REG(UGE_BASE + 0x0028)
/*
 * src color key for low value UGE_SCL
 */
#define UGE_SCL       __REG(UGE_BASE + 0x002C)
/*
 * dst color key for low value UGE_DCL
 */
#define UGE_DCL       __REG(UGE_BASE + 0x0030)
/*
 * clip 0 reg UGE_CLIP0
 */
#define UGE_CLIP0     __REG(UGE_BASE + 0x0034)
/*
 * clip 1 reg UGE_CLIP1
 */
#define UGE_CLIP1     __REG(UGE_BASE + 0x0038)
/*
 * command reg UGE_COMMAND
 */
#define UGE_COMMAND   __REG(UGE_BASE + 0x003C)
/*
 * pattern 0 UGE_P0
 */
#define UGE_P0        __REG(UGE_BASE + 0x0040)
#define UGE_P1        __REG(UGE_BASE + 0x0044)
#define UGE_P2        __REG(UGE_BASE + 0x0048)
#define UGE_P3        __REG(UGE_BASE + 0x004C)
#define UGE_P4        __REG(UGE_BASE + 0x0050)
#define UGE_P5        __REG(UGE_BASE + 0x0054)
#define UGE_P6        __REG(UGE_BASE + 0x0058)
#define UGE_P7        __REG(UGE_BASE + 0x005C)
#define UGE_P8        __REG(UGE_BASE + 0x0060)
#define UGE_P9        __REG(UGE_BASE + 0x0064)
#define UGE_P10       __REG(UGE_BASE + 0x0068)
#define UGE_P11       __REG(UGE_BASE + 0x006C)
#define UGE_P12       __REG(UGE_BASE + 0x0070)
#define UGE_P13       __REG(UGE_BASE + 0x0074)
#define UGE_P14       __REG(UGE_BASE + 0x0078)
#define UGE_P15       __REG(UGE_BASE + 0x007C)
#define UGE_P16       __REG(UGE_BASE + 0x0080)
#define UGE_P17       __REG(UGE_BASE + 0x0084)
#define UGE_P18       __REG(UGE_BASE + 0x0088)
#define UGE_P19       __REG(UGE_BASE + 0x008C)
#define UGE_P20       __REG(UGE_BASE + 0x0090)
#define UGE_P21       __REG(UGE_BASE + 0x0094)
#define UGE_P22       __REG(UGE_BASE + 0x0098)
#define UGE_P23       __REG(UGE_BASE + 0x009C)
#define UGE_P24       __REG(UGE_BASE + 0x00A0)
#define UGE_P25       __REG(UGE_BASE + 0x00A4)
#define UGE_P26       __REG(UGE_BASE + 0x00A8)
#define UGE_P27       __REG(UGE_BASE + 0x00AC)
#define UGE_P28       __REG(UGE_BASE + 0x00B0)
#define UGE_P29       __REG(UGE_BASE + 0x00B4)
#define UGE_P30       __REG(UGE_BASE + 0x00B8)
#define UGE_P31       __REG(UGE_BASE + 0x00BC)

#define UDE_CFG_DST_MASK	FMASK(2, 8)
#define UDE_CFG_DST8            FIELD(0x0, 2, 8)
#define UDE_CFG_DST16           FIELD(0x1, 2, 8)
#define UDE_CFG_DST24           FIELD(0x2, 2, 8)
#define UDE_CFG_DST32           FIELD(0x3, 2, 8)

/*
 * GDEN enable UDE_CFG_GDEN_ENABLE
 */
#define UDE_CFG_GDEN_ENABLE     FIELD(1, 1, 3)
/*
 * VDEN enable UDE_CFG_VDEN_ENABLE
 */
#define UDE_CFG_VDEN_ENABLE     FIELD(1, 1, 4)
/*
 * CDEN enable UDE_CFG_CDEN_ENABLE
 */
#define UDE_CFG_CDEN_ENABLE     FIELD(1, 1, 5)
/*
 * TIMEUP enable UDE_CFG_TIMEUP_ENABLE
 */
#define UDE_CFG_TIMEUP_ENABLE   FIELD(1, 1, 6)
