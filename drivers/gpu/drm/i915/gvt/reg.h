/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _GVT_REG_H
#define _GVT_REG_H

#define INTEL_GVT_PCI_CLASS_VGA_OTHER   0x80

#define INTEL_GVT_PCI_GMCH_CONTROL	0x50
#define   BDW_GMCH_GMS_SHIFT		8
#define   BDW_GMCH_GMS_MASK		0xff

#define INTEL_GVT_PCI_SWSCI		0xe8
#define   SWSCI_SCI_SELECT		(1 << 15)
#define   SWSCI_SCI_TRIGGER		1

#define INTEL_GVT_PCI_OPREGION		0xfc

#define INTEL_GVT_OPREGION_CLID		0x1AC
#define INTEL_GVT_OPREGION_SCIC		0x200
#define   OPREGION_SCIC_FUNC_MASK	0x1E
#define   OPREGION_SCIC_FUNC_SHIFT	1
#define   OPREGION_SCIC_SUBFUNC_MASK	0xFF00
#define   OPREGION_SCIC_SUBFUNC_SHIFT	8
#define   OPREGION_SCIC_EXIT_MASK	0xE0
#define INTEL_GVT_OPREGION_SCIC_F_GETBIOSDATA         4
#define INTEL_GVT_OPREGION_SCIC_F_GETBIOSCALLBACKS    6
#define INTEL_GVT_OPREGION_SCIC_SF_SUPPRTEDCALLS      0
#define INTEL_GVT_OPREGION_SCIC_SF_REQEUSTEDCALLBACKS 1
#define INTEL_GVT_OPREGION_PARM                   0x204

#define INTEL_GVT_OPREGION_PAGES	2
#define INTEL_GVT_OPREGION_SIZE		(INTEL_GVT_OPREGION_PAGES * PAGE_SIZE)
#define INTEL_GVT_OPREGION_VBT_OFFSET	0x400
#define INTEL_GVT_OPREGION_VBT_SIZE	\
		(INTEL_GVT_OPREGION_SIZE - INTEL_GVT_OPREGION_VBT_OFFSET)

#define VGT_SPRSTRIDE(pipe)	_PIPE(pipe, _SPRA_STRIDE, _PLANE_STRIDE_2_B)

#define _REG_701C0(pipe, plane) (0x701c0 + pipe * 0x1000 + (plane - 1) * 0x100)
#define _REG_701C4(pipe, plane) (0x701c4 + pipe * 0x1000 + (plane - 1) * 0x100)

#define SKL_FLIP_EVENT(pipe, plane) (PRIMARY_A_FLIP_DONE + (plane) * 3 + (pipe))

#define PLANE_CTL_ASYNC_FLIP		(1 << 9)
#define REG50080_FLIP_TYPE_MASK	0x3
#define REG50080_FLIP_TYPE_ASYNC	0x1

#define REG_50080(_pipe, _plane) ({ \
	typeof(_pipe) (p) = (_pipe); \
	typeof(_plane) (q) = (_plane); \
	(((p) == PIPE_A) ? (((q) == PLANE_PRIMARY) ? (_MMIO(0x50080)) : \
		(_MMIO(0x50090))) : \
	(((p) == PIPE_B) ? (((q) == PLANE_PRIMARY) ? (_MMIO(0x50088)) : \
		(_MMIO(0x50098))) : \
	(((p) == PIPE_C) ? (((q) == PLANE_PRIMARY) ? (_MMIO(0x5008C)) : \
		(_MMIO(0x5009C))) : \
		(_MMIO(0x50080))))); })

#define REG_50080_TO_PIPE(_reg) ({ \
	typeof(_reg) (reg) = (_reg); \
	(((reg) == 0x50080 || (reg) == 0x50090) ? (PIPE_A) : \
	(((reg) == 0x50088 || (reg) == 0x50098) ? (PIPE_B) : \
	(((reg) == 0x5008C || (reg) == 0x5009C) ? (PIPE_C) : \
	(INVALID_PIPE)))); })

#define REG_50080_TO_PLANE(_reg) ({ \
	typeof(_reg) (reg) = (_reg); \
	(((reg) == 0x50080 || (reg) == 0x50088 || (reg) == 0x5008C) ? \
		(PLANE_PRIMARY) : \
	(((reg) == 0x50090 || (reg) == 0x50098 || (reg) == 0x5009C) ? \
		(PLANE_SPRITE0) : (I915_MAX_PLANES))); })

#define GFX_MODE_BIT_SET_IN_MASK(val, bit) \
		((((bit) & 0xffff0000) == 0) && !!((val) & (((bit) << 16))))

#define IS_MASKED_BITS_ENABLED(_val, _b) \
		(((_val) & _MASKED_BIT_ENABLE(_b)) == _MASKED_BIT_ENABLE(_b))
#define IS_MASKED_BITS_DISABLED(_val, _b) \
		((_val) & _MASKED_BIT_DISABLE(_b))

#define FORCEWAKE_RENDER_GEN9_REG 0xa278
#define FORCEWAKE_ACK_RENDER_GEN9_REG 0x0D84
#define FORCEWAKE_BLITTER_GEN9_REG 0xa188
#define FORCEWAKE_ACK_BLITTER_GEN9_REG 0x130044
#define FORCEWAKE_MEDIA_GEN9_REG 0xa270
#define FORCEWAKE_ACK_MEDIA_GEN9_REG 0x0D88
#define FORCEWAKE_ACK_HSW_REG 0x130044

#define RB_HEAD_WRAP_CNT_MAX	((1 << 11) - 1)
#define RB_HEAD_WRAP_CNT_OFF	21
#define RB_HEAD_OFF_MASK	((1U << 21) - (1U << 2))
#define RB_TAIL_OFF_MASK	((1U << 21) - (1U << 3))
#define RB_TAIL_SIZE_MASK	((1U << 21) - (1U << 12))
#define _RING_CTL_BUF_SIZE(ctl) (((ctl) & RB_TAIL_SIZE_MASK) + \
		I915_GTT_PAGE_SIZE)

#define PCH_GPIO_BASE	_MMIO(0xc5010)

#define PCH_GMBUS0	_MMIO(0xc5100)
#define PCH_GMBUS1	_MMIO(0xc5104)
#define PCH_GMBUS2	_MMIO(0xc5108)
#define PCH_GMBUS3	_MMIO(0xc510c)
#define PCH_GMBUS4	_MMIO(0xc5110)
#define PCH_GMBUS5	_MMIO(0xc5120)

#define TRVATTL3PTRDW(i)	_MMIO(0x4de0 + (i) * 4)
#define TRNULLDETCT		_MMIO(0x4de8)
#define TRINVTILEDETCT		_MMIO(0x4dec)
#define TRVADR			_MMIO(0x4df0)
#define TRTTE			_MMIO(0x4df4)
#define RING_EXCC(base)		_MMIO((base) + 0x28)
#define RING_GFX_MODE(base)	_MMIO((base) + 0x29c)
#define VF_GUARDBAND		_MMIO(0x83a4)

#endif
