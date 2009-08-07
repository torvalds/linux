/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    dmacHw_reg.h
*
*  @brief   Definitions for low level DMA registers
*
*/
/****************************************************************************/

#ifndef _DMACHW_REG_H
#define _DMACHW_REG_H

#include <csp/stdint.h>
#include <mach/csp/mm_io.h>

/* Data type for 64 bit little endian register */
typedef struct {
	volatile uint32_t lo;	/* Lower 32 bit in little endian mode */
	volatile uint32_t hi;	/* Upper 32 bit in little endian mode */
} dmacHw_REG64_t;

/* Data type representing DMA channel registers */
typedef struct {
	dmacHw_REG64_t ChannelSar;	/*  Source Adress Register. 64 bits (upper 32 bits are reserved)
					   Address must be aligned to CTLx.SRC_TR_WIDTH.
					 */
	dmacHw_REG64_t ChannelDar;	/*  Destination Address Register.64 bits (upper 32 bits are reserved)
					   Address must be aligned to CTLx.DST_TR_WIDTH.
					 */
	dmacHw_REG64_t ChannelLlp;	/*  Link List Pointer.64 bits (upper 32 bits are reserved)
					   LLP contains the pointer to the next LLI for block chaining using linked lists.
					   If LLPis set to 0x0, then transfers using linked lists are not enabled.
					   Address MUST be aligned to a 32-bit boundary.
					 */
	dmacHw_REG64_t ChannelCtl;	/* Control Register. 64 bits */
	dmacHw_REG64_t ChannelSstat;	/* Source Status Register */
	dmacHw_REG64_t ChannelDstat;	/* Destination Status Register */
	dmacHw_REG64_t ChannelSstatAddr;	/* Source Status Address Register */
	dmacHw_REG64_t ChannelDstatAddr;	/* Destination Status Address Register */
	dmacHw_REG64_t ChannelConfig;	/* Channel Configuration Register */
	dmacHw_REG64_t SrcGather;	/* Source gather register */
	dmacHw_REG64_t DstScatter;	/* Destination scatter register */
} dmacHw_CH_REG_t;

/* Data type for RAW interrupt status registers */
typedef struct {
	dmacHw_REG64_t RawTfr;	/* Raw Status for IntTfr Interrupt */
	dmacHw_REG64_t RawBlock;	/* Raw Status for IntBlock Interrupt */
	dmacHw_REG64_t RawSrcTran;	/* Raw Status for IntSrcTran Interrupt */
	dmacHw_REG64_t RawDstTran;	/* Raw Status for IntDstTran Interrupt */
	dmacHw_REG64_t RawErr;	/* Raw Status for IntErr Interrupt */
} dmacHw_INT_RAW_t;

/* Data type for interrupt status registers */
typedef struct {
	dmacHw_REG64_t StatusTfr;	/* Status for IntTfr Interrupt */
	dmacHw_REG64_t StatusBlock;	/* Status for IntBlock Interrupt */
	dmacHw_REG64_t StatusSrcTran;	/* Status for IntSrcTran Interrupt */
	dmacHw_REG64_t StatusDstTran;	/* Status for IntDstTran Interrupt */
	dmacHw_REG64_t StatusErr;	/* Status for IntErr Interrupt */
} dmacHw_INT_STATUS_t;

/* Data type for interrupt mask registers*/
typedef struct {
	dmacHw_REG64_t MaskTfr;	/* Mask for IntTfr Interrupt */
	dmacHw_REG64_t MaskBlock;	/* Mask for IntBlock Interrupt */
	dmacHw_REG64_t MaskSrcTran;	/* Mask for IntSrcTran Interrupt */
	dmacHw_REG64_t MaskDstTran;	/* Mask for IntDstTran Interrupt */
	dmacHw_REG64_t MaskErr;	/* Mask for IntErr Interrupt */
} dmacHw_INT_MASK_t;

/* Data type for interrupt clear registers */
typedef struct {
	dmacHw_REG64_t ClearTfr;	/* Clear for IntTfr Interrupt */
	dmacHw_REG64_t ClearBlock;	/* Clear for IntBlock Interrupt */
	dmacHw_REG64_t ClearSrcTran;	/* Clear for IntSrcTran Interrupt */
	dmacHw_REG64_t ClearDstTran;	/* Clear for IntDstTran Interrupt */
	dmacHw_REG64_t ClearErr;	/* Clear for IntErr Interrupt */
	dmacHw_REG64_t StatusInt;	/* Status for each interrupt type */
} dmacHw_INT_CLEAR_t;

/* Data type for software handshaking registers */
typedef struct {
	dmacHw_REG64_t ReqSrcReg;	/* Source Software Transaction Request Register */
	dmacHw_REG64_t ReqDstReg;	/* Destination Software Transaction Request Register */
	dmacHw_REG64_t SglReqSrcReg;	/* Single Source Transaction Request Register */
	dmacHw_REG64_t SglReqDstReg;	/* Single Destination Transaction Request Register */
	dmacHw_REG64_t LstSrcReg;	/* Last Source Transaction Request Register */
	dmacHw_REG64_t LstDstReg;	/* Last Destination Transaction Request Register */
} dmacHw_SW_HANDSHAKE_t;

/* Data type for misc. registers */
typedef struct {
	dmacHw_REG64_t DmaCfgReg;	/* DMA Configuration Register */
	dmacHw_REG64_t ChEnReg;	/* DMA Channel Enable Register */
	dmacHw_REG64_t DmaIdReg;	/* DMA ID Register */
	dmacHw_REG64_t DmaTestReg;	/* DMA Test Register */
	dmacHw_REG64_t Reserved0;	/* Reserved */
	dmacHw_REG64_t Reserved1;	/* Reserved */
	dmacHw_REG64_t CompParm6;	/* Component Parameter 6 */
	dmacHw_REG64_t CompParm5;	/* Component Parameter 5 */
	dmacHw_REG64_t CompParm4;	/* Component Parameter 4 */
	dmacHw_REG64_t CompParm3;	/* Component Parameter 3 */
	dmacHw_REG64_t CompParm2;	/* Component Parameter 2 */
	dmacHw_REG64_t CompParm1;	/* Component Parameter 1 */
	dmacHw_REG64_t CompId;	/* Compoent ID */
} dmacHw_MISC_t;

/* Base registers */
#define dmacHw_0_MODULE_BASE_ADDR        (char *) MM_IO_BASE_DMA0	/* DMAC 0 module's base address */
#define dmacHw_1_MODULE_BASE_ADDR        (char *) MM_IO_BASE_DMA1	/* DMAC 1 module's base address */

extern uint32_t dmaChannelCount_0;
extern uint32_t dmaChannelCount_1;

/* Define channel specific registers */
#define dmacHw_CHAN_BASE(module, chan)          ((dmacHw_CH_REG_t *) ((char *)((module) ? dmacHw_1_MODULE_BASE_ADDR : dmacHw_0_MODULE_BASE_ADDR) + ((chan) * sizeof(dmacHw_CH_REG_t))))

/* Raw interrupt status registers */
#define dmacHw_REG_INT_RAW_BASE(module)         ((char *)dmacHw_CHAN_BASE((module), ((module) ? dmaChannelCount_1 : dmaChannelCount_0)))
#define dmacHw_REG_INT_RAW_TRAN(module)         (((dmacHw_INT_RAW_t *) dmacHw_REG_INT_RAW_BASE((module)))->RawTfr.lo)
#define dmacHw_REG_INT_RAW_BLOCK(module)        (((dmacHw_INT_RAW_t *) dmacHw_REG_INT_RAW_BASE((module)))->RawBlock.lo)
#define dmacHw_REG_INT_RAW_STRAN(module)        (((dmacHw_INT_RAW_t *) dmacHw_REG_INT_RAW_BASE((module)))->RawSrcTran.lo)
#define dmacHw_REG_INT_RAW_DTRAN(module)        (((dmacHw_INT_RAW_t *) dmacHw_REG_INT_RAW_BASE((module)))->RawDstTran.lo)
#define dmacHw_REG_INT_RAW_ERROR(module)        (((dmacHw_INT_RAW_t *) dmacHw_REG_INT_RAW_BASE((module)))->RawErr.lo)

/* Interrupt status registers */
#define dmacHw_REG_INT_STAT_BASE(module)        ((char *)(dmacHw_REG_INT_RAW_BASE((module)) + sizeof(dmacHw_INT_RAW_t)))
#define dmacHw_REG_INT_STAT_TRAN(module)        (((dmacHw_INT_STATUS_t *) dmacHw_REG_INT_STAT_BASE((module)))->StatusTfr.lo)
#define dmacHw_REG_INT_STAT_BLOCK(module)       (((dmacHw_INT_STATUS_t *) dmacHw_REG_INT_STAT_BASE((module)))->StatusBlock.lo)
#define dmacHw_REG_INT_STAT_STRAN(module)       (((dmacHw_INT_STATUS_t *) dmacHw_REG_INT_STAT_BASE((module)))->StatusSrcTran.lo)
#define dmacHw_REG_INT_STAT_DTRAN(module)       (((dmacHw_INT_STATUS_t *) dmacHw_REG_INT_STAT_BASE((module)))->StatusDstTran.lo)
#define dmacHw_REG_INT_STAT_ERROR(module)       (((dmacHw_INT_STATUS_t *) dmacHw_REG_INT_STAT_BASE((module)))->StatusErr.lo)

/* Interrupt status registers */
#define dmacHw_REG_INT_MASK_BASE(module)        ((char *)(dmacHw_REG_INT_STAT_BASE((module)) + sizeof(dmacHw_INT_STATUS_t)))
#define dmacHw_REG_INT_MASK_TRAN(module)        (((dmacHw_INT_MASK_t *) dmacHw_REG_INT_MASK_BASE((module)))->MaskTfr.lo)
#define dmacHw_REG_INT_MASK_BLOCK(module)       (((dmacHw_INT_MASK_t *) dmacHw_REG_INT_MASK_BASE((module)))->MaskBlock.lo)
#define dmacHw_REG_INT_MASK_STRAN(module)       (((dmacHw_INT_MASK_t *) dmacHw_REG_INT_MASK_BASE((module)))->MaskSrcTran.lo)
#define dmacHw_REG_INT_MASK_DTRAN(module)       (((dmacHw_INT_MASK_t *) dmacHw_REG_INT_MASK_BASE((module)))->MaskDstTran.lo)
#define dmacHw_REG_INT_MASK_ERROR(module)       (((dmacHw_INT_MASK_t *) dmacHw_REG_INT_MASK_BASE((module)))->MaskErr.lo)

/* Interrupt clear registers */
#define dmacHw_REG_INT_CLEAR_BASE(module)       ((char *)(dmacHw_REG_INT_MASK_BASE((module)) + sizeof(dmacHw_INT_MASK_t)))
#define dmacHw_REG_INT_CLEAR_TRAN(module)       (((dmacHw_INT_CLEAR_t *) dmacHw_REG_INT_CLEAR_BASE((module)))->ClearTfr.lo)
#define dmacHw_REG_INT_CLEAR_BLOCK(module)      (((dmacHw_INT_CLEAR_t *) dmacHw_REG_INT_CLEAR_BASE((module)))->ClearBlock.lo)
#define dmacHw_REG_INT_CLEAR_STRAN(module)      (((dmacHw_INT_CLEAR_t *) dmacHw_REG_INT_CLEAR_BASE((module)))->ClearSrcTran.lo)
#define dmacHw_REG_INT_CLEAR_DTRAN(module)      (((dmacHw_INT_CLEAR_t *) dmacHw_REG_INT_CLEAR_BASE((module)))->ClearDstTran.lo)
#define dmacHw_REG_INT_CLEAR_ERROR(module)      (((dmacHw_INT_CLEAR_t *) dmacHw_REG_INT_CLEAR_BASE((module)))->ClearErr.lo)
#define dmacHw_REG_INT_STATUS(module)           (((dmacHw_INT_CLEAR_t *) dmacHw_REG_INT_CLEAR_BASE((module)))->StatusInt.lo)

/* Software handshaking registers */
#define dmacHw_REG_SW_HS_BASE(module)           ((char *)(dmacHw_REG_INT_CLEAR_BASE((module)) + sizeof(dmacHw_INT_CLEAR_t)))
#define dmacHw_REG_SW_HS_SRC_REQ(module)        (((dmacHw_SW_HANDSHAKE_t *) dmacHw_REG_SW_HS_BASE((module)))->ReqSrcReg.lo)
#define dmacHw_REG_SW_HS_DST_REQ(module)        (((dmacHw_SW_HANDSHAKE_t *) dmacHw_REG_SW_HS_BASE((module)))->ReqDstReg.lo)
#define dmacHw_REG_SW_HS_SRC_SGL_REQ(module)    (((dmacHw_SW_HANDSHAKE_t *) dmacHw_REG_SW_HS_BASE((module)))->SglReqSrcReg.lo)
#define dmacHw_REG_SW_HS_DST_SGL_REQ(module)    (((dmacHw_SW_HANDSHAKE_t *) dmacHw_REG_SW_HS_BASE((module)))->SglReqDstReg.lo)
#define dmacHw_REG_SW_HS_SRC_LST_REQ(module)    (((dmacHw_SW_HANDSHAKE_t *) dmacHw_REG_SW_HS_BASE((module)))->LstSrcReg.lo)
#define dmacHw_REG_SW_HS_DST_LST_REQ(module)    (((dmacHw_SW_HANDSHAKE_t *) dmacHw_REG_SW_HS_BASE((module)))->LstDstReg.lo)

/* Miscellaneous registers */
#define dmacHw_REG_MISC_BASE(module)            ((char *)(dmacHw_REG_SW_HS_BASE((module)) + sizeof(dmacHw_SW_HANDSHAKE_t)))
#define dmacHw_REG_MISC_CFG(module)             (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->DmaCfgReg.lo)
#define dmacHw_REG_MISC_CH_ENABLE(module)       (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->ChEnReg.lo)
#define dmacHw_REG_MISC_ID(module)              (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->DmaIdReg.lo)
#define dmacHw_REG_MISC_TEST(module)            (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->DmaTestReg.lo)
#define dmacHw_REG_MISC_COMP_PARAM1_LO(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm1.lo)
#define dmacHw_REG_MISC_COMP_PARAM1_HI(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm1.hi)
#define dmacHw_REG_MISC_COMP_PARAM2_LO(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm2.lo)
#define dmacHw_REG_MISC_COMP_PARAM2_HI(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm2.hi)
#define dmacHw_REG_MISC_COMP_PARAM3_LO(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm3.lo)
#define dmacHw_REG_MISC_COMP_PARAM3_HI(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm3.hi)
#define dmacHw_REG_MISC_COMP_PARAM4_LO(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm4.lo)
#define dmacHw_REG_MISC_COMP_PARAM4_HI(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm4.hi)
#define dmacHw_REG_MISC_COMP_PARAM5_LO(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm5.lo)
#define dmacHw_REG_MISC_COMP_PARAM5_HI(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm5.hi)
#define dmacHw_REG_MISC_COMP_PARAM6_LO(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm6.lo)
#define dmacHw_REG_MISC_COMP_PARAM6_HI(module)  (((dmacHw_MISC_t *) dmacHw_REG_MISC_BASE((module)))->CompParm6.hi)

/* Channel control registers */
#define dmacHw_REG_SAR(module, chan)            (dmacHw_CHAN_BASE((module), (chan))->ChannelSar.lo)
#define dmacHw_REG_DAR(module, chan)            (dmacHw_CHAN_BASE((module), (chan))->ChannelDar.lo)
#define dmacHw_REG_LLP(module, chan)            (dmacHw_CHAN_BASE((module), (chan))->ChannelLlp.lo)

#define dmacHw_REG_CTL_LO(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->ChannelCtl.lo)
#define dmacHw_REG_CTL_HI(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->ChannelCtl.hi)

#define dmacHw_REG_SSTAT(module, chan)          (dmacHw_CHAN_BASE((module), (chan))->ChannelSstat.lo)
#define dmacHw_REG_DSTAT(module, chan)          (dmacHw_CHAN_BASE((module), (chan))->ChannelDstat.lo)
#define dmacHw_REG_SSTATAR(module, chan)        (dmacHw_CHAN_BASE((module), (chan))->ChannelSstatAddr.lo)
#define dmacHw_REG_DSTATAR(module, chan)        (dmacHw_CHAN_BASE((module), (chan))->ChannelDstatAddr.lo)

#define dmacHw_REG_CFG_LO(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->ChannelConfig.lo)
#define dmacHw_REG_CFG_HI(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->ChannelConfig.hi)

#define dmacHw_REG_SGR_LO(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->SrcGather.lo)
#define dmacHw_REG_SGR_HI(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->SrcGather.hi)

#define dmacHw_REG_DSR_LO(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->DstScatter.lo)
#define dmacHw_REG_DSR_HI(module, chan)         (dmacHw_CHAN_BASE((module), (chan))->DstScatter.hi)

#define INT_STATUS_MASK(channel)                (0x00000001 << (channel))
#define CHANNEL_BUSY(mod, channel)              (dmacHw_REG_MISC_CH_ENABLE((mod)) & (0x00000001 << (channel)))

/* Bit mask for REG_DMACx_CTL_LO */

#define dmacHw_REG_CTL_INT_EN                       0x00000001	/* Channel interrupt enable */

#define dmacHw_REG_CTL_DST_TR_WIDTH_MASK            0x0000000E	/* Destination transaction width mask */
#define dmacHw_REG_CTL_DST_TR_WIDTH_SHIFT           1
#define dmacHw_REG_CTL_DST_TR_WIDTH_8               0x00000000	/* Destination transaction width 8 bit */
#define dmacHw_REG_CTL_DST_TR_WIDTH_16              0x00000002	/* Destination transaction width 16 bit */
#define dmacHw_REG_CTL_DST_TR_WIDTH_32              0x00000004	/* Destination transaction width 32 bit */
#define dmacHw_REG_CTL_DST_TR_WIDTH_64              0x00000006	/* Destination transaction width 64 bit */

#define dmacHw_REG_CTL_SRC_TR_WIDTH_MASK            0x00000070	/* Source transaction width mask */
#define dmacHw_REG_CTL_SRC_TR_WIDTH_SHIFT           4
#define dmacHw_REG_CTL_SRC_TR_WIDTH_8               0x00000000	/* Source transaction width 8 bit */
#define dmacHw_REG_CTL_SRC_TR_WIDTH_16              0x00000010	/* Source transaction width 16 bit */
#define dmacHw_REG_CTL_SRC_TR_WIDTH_32              0x00000020	/* Source transaction width 32 bit */
#define dmacHw_REG_CTL_SRC_TR_WIDTH_64              0x00000030	/* Source transaction width 64 bit */

#define dmacHw_REG_CTL_DS_ENABLE                    0x00040000	/* Destination scatter enable */
#define dmacHw_REG_CTL_SG_ENABLE                    0x00020000	/* Source gather enable */

#define dmacHw_REG_CTL_DINC_MASK                    0x00000180	/* Destination address inc/dec mask */
#define dmacHw_REG_CTL_DINC_INC                     0x00000000	/* Destination address increment */
#define dmacHw_REG_CTL_DINC_DEC                     0x00000080	/* Destination address decrement */
#define dmacHw_REG_CTL_DINC_NC                      0x00000100	/* Destination address no change */

#define dmacHw_REG_CTL_SINC_MASK                    0x00000600	/* Source address inc/dec mask */
#define dmacHw_REG_CTL_SINC_INC                     0x00000000	/* Source address increment */
#define dmacHw_REG_CTL_SINC_DEC                     0x00000200	/* Source address decrement */
#define dmacHw_REG_CTL_SINC_NC                      0x00000400	/* Source address no change */

#define dmacHw_REG_CTL_DST_MSIZE_MASK               0x00003800	/* Destination burst transaction length */
#define dmacHw_REG_CTL_DST_MSIZE_0                  0x00000000	/* No Destination burst */
#define dmacHw_REG_CTL_DST_MSIZE_4                  0x00000800	/* Destination burst transaction length 4 */
#define dmacHw_REG_CTL_DST_MSIZE_8                  0x00001000	/* Destination burst transaction length 8 */
#define dmacHw_REG_CTL_DST_MSIZE_16                 0x00001800	/* Destination burst transaction length 16 */

#define dmacHw_REG_CTL_SRC_MSIZE_MASK               0x0001C000	/* Source burst transaction length */
#define dmacHw_REG_CTL_SRC_MSIZE_0                  0x00000000	/* No Source burst */
#define dmacHw_REG_CTL_SRC_MSIZE_4                  0x00004000	/* Source burst transaction length 4 */
#define dmacHw_REG_CTL_SRC_MSIZE_8                  0x00008000	/* Source burst transaction length 8 */
#define dmacHw_REG_CTL_SRC_MSIZE_16                 0x0000C000	/* Source burst transaction length 16 */

#define dmacHw_REG_CTL_TTFC_MASK                    0x00700000	/* Transfer type and flow controller */
#define dmacHw_REG_CTL_TTFC_MM_DMAC                 0x00000000	/* Memory to Memory with DMAC as flow controller */
#define dmacHw_REG_CTL_TTFC_MP_DMAC                 0x00100000	/* Memory to Peripheral with DMAC as flow controller */
#define dmacHw_REG_CTL_TTFC_PM_DMAC                 0x00200000	/* Peripheral to Memory with DMAC as flow controller */
#define dmacHw_REG_CTL_TTFC_PP_DMAC                 0x00300000	/* Peripheral to Peripheral with DMAC as flow controller */
#define dmacHw_REG_CTL_TTFC_PM_PERI                 0x00400000	/* Peripheral to Memory with Peripheral as flow controller */
#define dmacHw_REG_CTL_TTFC_PP_SPERI                0x00500000	/* Peripheral to Peripheral with Source Peripheral as flow controller */
#define dmacHw_REG_CTL_TTFC_MP_PERI                 0x00600000	/* Memory to Peripheral with Peripheral as flow controller */
#define dmacHw_REG_CTL_TTFC_PP_DPERI                0x00700000	/* Peripheral to Peripheral with Destination Peripheral as flow controller */

#define dmacHw_REG_CTL_DMS_MASK                     0x01800000	/* Destination AHB master interface */
#define dmacHw_REG_CTL_DMS_1                        0x00000000	/* Destination AHB master interface 1 */
#define dmacHw_REG_CTL_DMS_2                        0x00800000	/* Destination AHB master interface 2 */

#define dmacHw_REG_CTL_SMS_MASK                     0x06000000	/* Source AHB master interface */
#define dmacHw_REG_CTL_SMS_1                        0x00000000	/* Source AHB master interface 1 */
#define dmacHw_REG_CTL_SMS_2                        0x02000000	/* Source AHB master interface 2 */

#define dmacHw_REG_CTL_LLP_DST_EN                   0x08000000	/* Block chaining enable for destination side */
#define dmacHw_REG_CTL_LLP_SRC_EN                   0x10000000	/* Block chaining enable for source side */

/* Bit mask for REG_DMACx_CTL_HI */
#define dmacHw_REG_CTL_BLOCK_TS_MASK                0x00000FFF	/* Block transfer size */
#define dmacHw_REG_CTL_DONE                         0x00001000	/* Block trasnfer done */

/* Bit mask for REG_DMACx_CFG_LO */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_SHIFT                  5	/* Channel priority shift */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_MASK          0x000000E0	/* Channel priority mask */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_0             0x00000000	/* Channel priority 0 */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_1             0x00000020	/* Channel priority 1 */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_2             0x00000040	/* Channel priority 2 */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_3             0x00000060	/* Channel priority 3 */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_4             0x00000080	/* Channel priority 4 */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_5             0x000000A0	/* Channel priority 5 */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_6             0x000000C0	/* Channel priority 6 */
#define dmacHw_REG_CFG_LO_CH_PRIORITY_7             0x000000E0	/* Channel priority 7 */

#define dmacHw_REG_CFG_LO_CH_SUSPEND                0x00000100	/* Channel suspend */
#define dmacHw_REG_CFG_LO_CH_FIFO_EMPTY             0x00000200	/* Channel FIFO empty */
#define dmacHw_REG_CFG_LO_DST_CH_SW_HS              0x00000400	/* Destination channel SW handshaking */
#define dmacHw_REG_CFG_LO_SRC_CH_SW_HS              0x00000800	/* Source channel SW handshaking */

#define dmacHw_REG_CFG_LO_CH_LOCK_MASK              0x00003000	/* Channel locking mask */
#define dmacHw_REG_CFG_LO_CH_LOCK_DMA               0x00000000	/* Channel lock over the entire DMA transfer operation */
#define dmacHw_REG_CFG_LO_CH_LOCK_BLOCK             0x00001000	/* Channel lock over the block transfer operation */
#define dmacHw_REG_CFG_LO_CH_LOCK_TRANS             0x00002000	/* Channel lock over the transaction */
#define dmacHw_REG_CFG_LO_CH_LOCK_ENABLE            0x00010000	/* Channel lock enable */

#define dmacHw_REG_CFG_LO_BUS_LOCK_MASK             0x0000C000	/* Bus locking mask */
#define dmacHw_REG_CFG_LO_BUS_LOCK_DMA              0x00000000	/* Bus lock over the entire DMA transfer operation */
#define dmacHw_REG_CFG_LO_BUS_LOCK_BLOCK            0x00004000	/* Bus lock over the block transfer operation */
#define dmacHw_REG_CFG_LO_BUS_LOCK_TRANS            0x00008000	/* Bus lock over the transaction */
#define dmacHw_REG_CFG_LO_BUS_LOCK_ENABLE           0x00020000	/* Bus lock enable */

#define dmacHw_REG_CFG_LO_DST_HS_POLARITY_LOW       0x00040000	/* Destination channel handshaking signal polarity low */
#define dmacHw_REG_CFG_LO_SRC_HS_POLARITY_LOW       0x00080000	/* Source channel handshaking signal polarity low */

#define dmacHw_REG_CFG_LO_MAX_AMBA_BURST_LEN_MASK   0x3FF00000	/* Maximum AMBA burst length */

#define dmacHw_REG_CFG_LO_AUTO_RELOAD_SRC           0x40000000	/* Source address auto reload */
#define dmacHw_REG_CFG_LO_AUTO_RELOAD_DST           0x80000000	/* Destination address auto reload */

/* Bit mask for REG_DMACx_CFG_HI */
#define dmacHw_REG_CFG_HI_FC_DST_READY              0x00000001	/* Source transaction request is serviced when destination is ready */
#define dmacHw_REG_CFG_HI_FIFO_ENOUGH               0x00000002	/* Initiate burst transaction when enough data in available in FIFO */

#define dmacHw_REG_CFG_HI_AHB_HPROT_MASK            0x0000001C	/* AHB protection mask */
#define dmacHw_REG_CFG_HI_AHB_HPROT_1               0x00000004	/* AHB protection 1 */
#define dmacHw_REG_CFG_HI_AHB_HPROT_2               0x00000008	/* AHB protection 2 */
#define dmacHw_REG_CFG_HI_AHB_HPROT_3               0x00000010	/* AHB protection 3 */

#define dmacHw_REG_CFG_HI_UPDATE_DST_STAT           0x00000020	/* Destination status update enable */
#define dmacHw_REG_CFG_HI_UPDATE_SRC_STAT           0x00000040	/* Source status update enable */

#define dmacHw_REG_CFG_HI_SRC_PERI_INTF_MASK        0x00000780	/* Source peripheral hardware interface mask */
#define dmacHw_REG_CFG_HI_DST_PERI_INTF_MASK        0x00007800	/* Destination peripheral hardware interface mask */

/* DMA Configuration Parameters */
#define dmacHw_REG_COMP_PARAM_NUM_CHANNELS          0x00000700	/* Number of channels */
#define dmacHw_REG_COMP_PARAM_NUM_INTERFACE         0x00001800	/* Number of master interface */
#define dmacHw_REG_COMP_PARAM_MAX_BLK_SIZE          0x0000000f	/* Maximum brust size */
#define dmacHw_REG_COMP_PARAM_DATA_WIDTH            0x00006000	/* Data transfer width */

/* Define GET/SET macros to program the registers */
#define dmacHw_SET_SAR(module, channel, addr)          (dmacHw_REG_SAR((module), (channel)) = (uint32_t) (addr))
#define dmacHw_SET_DAR(module, channel, addr)          (dmacHw_REG_DAR((module), (channel)) = (uint32_t) (addr))
#define dmacHw_SET_LLP(module, channel, ptr)           (dmacHw_REG_LLP((module), (channel)) = (uint32_t) (ptr))

#define dmacHw_GET_SSTAT(module, channel)              (dmacHw_REG_SSTAT((module), (channel)))
#define dmacHw_GET_DSTAT(module, channel)              (dmacHw_REG_DSTAT((module), (channel)))

#define dmacHw_SET_SSTATAR(module, channel, addr)      (dmacHw_REG_SSTATAR((module), (channel)) = (uint32_t) (addr))
#define dmacHw_SET_DSTATAR(module, channel, addr)      (dmacHw_REG_DSTATAR((module), (channel)) = (uint32_t) (addr))

#define dmacHw_SET_CONTROL_LO(module, channel, ctl)    (dmacHw_REG_CTL_LO((module), (channel)) |= (ctl))
#define dmacHw_RESET_CONTROL_LO(module, channel)       (dmacHw_REG_CTL_LO((module), (channel)) = 0)
#define dmacHw_GET_CONTROL_LO(module, channel)         (dmacHw_REG_CTL_LO((module), (channel)))

#define dmacHw_SET_CONTROL_HI(module, channel, ctl)    (dmacHw_REG_CTL_HI((module), (channel)) |= (ctl))
#define dmacHw_RESET_CONTROL_HI(module, channel)       (dmacHw_REG_CTL_HI((module), (channel)) = 0)
#define dmacHw_GET_CONTROL_HI(module, channel)         (dmacHw_REG_CTL_HI((module), (channel)))

#define dmacHw_GET_BLOCK_SIZE(module, channel)         (dmacHw_REG_CTL_HI((module), (channel)) & dmacHw_REG_CTL_BLOCK_TS_MASK)
#define dmacHw_DMA_COMPLETE(module, channel)           (dmacHw_REG_CTL_HI((module), (channel)) & dmacHw_REG_CTL_DONE)

#define dmacHw_SET_CONFIG_LO(module, channel, cfg)     (dmacHw_REG_CFG_LO((module), (channel)) |= (cfg))
#define dmacHw_RESET_CONFIG_LO(module, channel)        (dmacHw_REG_CFG_LO((module), (channel)) = 0)
#define dmacHw_GET_CONFIG_LO(module, channel)          (dmacHw_REG_CFG_LO((module), (channel)))
#define dmacHw_SET_AMBA_BUSRT_LEN(module, channel, len)    (dmacHw_REG_CFG_LO((module), (channel)) = (dmacHw_REG_CFG_LO((module), (channel)) & ~(dmacHw_REG_CFG_LO_MAX_AMBA_BURST_LEN_MASK)) | (((len) << 20) & dmacHw_REG_CFG_LO_MAX_AMBA_BURST_LEN_MASK))
#define dmacHw_SET_CHANNEL_PRIORITY(module, channel, prio) (dmacHw_REG_CFG_LO((module), (channel)) = (dmacHw_REG_CFG_LO((module), (channel)) & ~(dmacHw_REG_CFG_LO_CH_PRIORITY_MASK)) | (prio))
#define dmacHw_SET_AHB_HPROT(module, channel, protect)  (dmacHw_REG_CFG_HI(module, channel) = (dmacHw_REG_CFG_HI((module), (channel)) & ~(dmacHw_REG_CFG_HI_AHB_HPROT_MASK)) | (protect))

#define dmacHw_SET_CONFIG_HI(module, channel, cfg)      (dmacHw_REG_CFG_HI((module), (channel)) |= (cfg))
#define dmacHw_RESET_CONFIG_HI(module, channel)         (dmacHw_REG_CFG_HI((module), (channel)) = 0)
#define dmacHw_GET_CONFIG_HI(module, channel)           (dmacHw_REG_CFG_HI((module), (channel)))
#define dmacHw_SET_SRC_PERI_INTF(module, channel, intf) (dmacHw_REG_CFG_HI((module), (channel)) = (dmacHw_REG_CFG_HI((module), (channel)) & ~(dmacHw_REG_CFG_HI_SRC_PERI_INTF_MASK)) | (((intf) << 7) & dmacHw_REG_CFG_HI_SRC_PERI_INTF_MASK))
#define dmacHw_SRC_PERI_INTF(intf)                      (((intf) << 7) & dmacHw_REG_CFG_HI_SRC_PERI_INTF_MASK)
#define dmacHw_SET_DST_PERI_INTF(module, channel, intf) (dmacHw_REG_CFG_HI((module), (channel)) = (dmacHw_REG_CFG_HI((module), (channel)) & ~(dmacHw_REG_CFG_HI_DST_PERI_INTF_MASK)) | (((intf) << 11) & dmacHw_REG_CFG_HI_DST_PERI_INTF_MASK))
#define dmacHw_DST_PERI_INTF(intf)                      (((intf) << 11) & dmacHw_REG_CFG_HI_DST_PERI_INTF_MASK)

#define dmacHw_DMA_START(module, channel)              (dmacHw_REG_MISC_CH_ENABLE((module)) = (0x00000001 << ((channel) + 8)) | (0x00000001 << (channel)))
#define dmacHw_DMA_STOP(module, channel)               (dmacHw_REG_MISC_CH_ENABLE((module)) = (0x00000001 << ((channel) + 8)))
#define dmacHw_DMA_ENABLE(module)                      (dmacHw_REG_MISC_CFG((module)) = 1)
#define dmacHw_DMA_DISABLE(module)                     (dmacHw_REG_MISC_CFG((module)) = 0)

#define dmacHw_TRAN_INT_ENABLE(module, channel)        (dmacHw_REG_INT_MASK_TRAN((module)) = (0x00000001 << ((channel) + 8)) | (0x00000001 << (channel)))
#define dmacHw_BLOCK_INT_ENABLE(module, channel)       (dmacHw_REG_INT_MASK_BLOCK((module)) = (0x00000001 << ((channel) + 8)) | (0x00000001 << (channel)))
#define dmacHw_ERROR_INT_ENABLE(module, channel)       (dmacHw_REG_INT_MASK_ERROR((module)) = (0x00000001 << ((channel) + 8)) | (0x00000001 << (channel)))

#define dmacHw_TRAN_INT_DISABLE(module, channel)       (dmacHw_REG_INT_MASK_TRAN((module)) = (0x00000001 << ((channel) + 8)))
#define dmacHw_BLOCK_INT_DISABLE(module, channel)      (dmacHw_REG_INT_MASK_BLOCK((module)) = (0x00000001 << ((channel) + 8)))
#define dmacHw_ERROR_INT_DISABLE(module, channel)      (dmacHw_REG_INT_MASK_ERROR((module)) = (0x00000001 << ((channel) + 8)))
#define dmacHw_STRAN_INT_DISABLE(module, channel)      (dmacHw_REG_INT_MASK_STRAN((module)) = (0x00000001 << ((channel) + 8)))
#define dmacHw_DTRAN_INT_DISABLE(module, channel)      (dmacHw_REG_INT_MASK_DTRAN((module)) = (0x00000001 << ((channel) + 8)))

#define dmacHw_TRAN_INT_CLEAR(module, channel)         (dmacHw_REG_INT_CLEAR_TRAN((module)) = (0x00000001 << (channel)))
#define dmacHw_BLOCK_INT_CLEAR(module, channel)        (dmacHw_REG_INT_CLEAR_BLOCK((module)) = (0x00000001 << (channel)))
#define dmacHw_ERROR_INT_CLEAR(module, channel)        (dmacHw_REG_INT_CLEAR_ERROR((module)) = (0x00000001 << (channel)))

#define dmacHw_GET_NUM_CHANNEL(module)                 (((dmacHw_REG_MISC_COMP_PARAM1_HI((module)) & dmacHw_REG_COMP_PARAM_NUM_CHANNELS) >> 8) + 1)
#define dmacHw_GET_NUM_INTERFACE(module)               (((dmacHw_REG_MISC_COMP_PARAM1_HI((module)) & dmacHw_REG_COMP_PARAM_NUM_INTERFACE) >> 11) + 1)
#define dmacHw_GET_MAX_BLOCK_SIZE(module, channel)     ((dmacHw_REG_MISC_COMP_PARAM1_LO((module)) >> (4 * (channel))) & dmacHw_REG_COMP_PARAM_MAX_BLK_SIZE)
#define dmacHw_GET_CHANNEL_DATA_WIDTH(module, channel) ((dmacHw_REG_MISC_COMP_PARAM1_HI((module)) & dmacHw_REG_COMP_PARAM_DATA_WIDTH) >> 13)

#endif /* _DMACHW_REG_H */
