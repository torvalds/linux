/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <mach/irqs.h>
#include <mach/iommu.h>

static struct resource msm_iommu_jpegd_resources[] = {
	{
		.start = 0x07300000,
		.end   = 0x07300000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_JPEGD_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_JPEGD_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_JPEGD_CB_SC_SECURE_IRQ,
		.end   = SMMU_JPEGD_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_vpe_resources[] = {
	{
		.start = 0x07400000,
		.end   = 0x07400000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_VPE_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_VPE_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_VPE_CB_SC_SECURE_IRQ,
		.end   = SMMU_VPE_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_mdp0_resources[] = {
	{
		.start = 0x07500000,
		.end   = 0x07500000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_MDP0_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_MDP0_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_MDP0_CB_SC_SECURE_IRQ,
		.end   = SMMU_MDP0_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_mdp1_resources[] = {
	{
		.start = 0x07600000,
		.end   = 0x07600000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_MDP1_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_MDP1_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_MDP1_CB_SC_SECURE_IRQ,
		.end   = SMMU_MDP1_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_rot_resources[] = {
	{
		.start = 0x07700000,
		.end   = 0x07700000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_ROT_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_ROT_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_ROT_CB_SC_SECURE_IRQ,
		.end   = SMMU_ROT_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_ijpeg_resources[] = {
	{
		.start = 0x07800000,
		.end   = 0x07800000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_IJPEG_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_IJPEG_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_IJPEG_CB_SC_SECURE_IRQ,
		.end   = SMMU_IJPEG_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_vfe_resources[] = {
	{
		.start = 0x07900000,
		.end   = 0x07900000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_VFE_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_VFE_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_VFE_CB_SC_SECURE_IRQ,
		.end   = SMMU_VFE_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_vcodec_a_resources[] = {
	{
		.start = 0x07A00000,
		.end   = 0x07A00000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_VCODEC_A_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_VCODEC_A_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_VCODEC_A_CB_SC_SECURE_IRQ,
		.end   = SMMU_VCODEC_A_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_vcodec_b_resources[] = {
	{
		.start = 0x07B00000,
		.end   = 0x07B00000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_VCODEC_B_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_VCODEC_B_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_VCODEC_B_CB_SC_SECURE_IRQ,
		.end   = SMMU_VCODEC_B_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_gfx3d_resources[] = {
	{
		.start = 0x07C00000,
		.end   = 0x07C00000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_GFX3D_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_GFX3D_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_GFX3D_CB_SC_SECURE_IRQ,
		.end   = SMMU_GFX3D_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_gfx2d0_resources[] = {
	{
		.start = 0x07D00000,
		.end   = 0x07D00000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_GFX2D0_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_GFX2D0_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_GFX2D0_CB_SC_SECURE_IRQ,
		.end   = SMMU_GFX2D0_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource msm_iommu_gfx2d1_resources[] = {
	{
		.start = 0x07E00000,
		.end   = 0x07E00000 + SZ_1M - 1,
		.name  = "physbase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "nonsecure_irq",
		.start = SMMU_GFX2D1_CB_SC_NON_SECURE_IRQ,
		.end   = SMMU_GFX2D1_CB_SC_NON_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "secure_irq",
		.start = SMMU_GFX2D1_CB_SC_SECURE_IRQ,
		.end   = SMMU_GFX2D1_CB_SC_SECURE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device msm_root_iommu_dev = {
	.name = "msm_iommu",
	.id = -1,
};

static struct msm_iommu_dev jpegd_iommu = {
	.name = "jpegd",
	.ncb = 2,
};

static struct msm_iommu_dev vpe_iommu = {
	.name = "vpe",
	.ncb = 2,
};

static struct msm_iommu_dev mdp0_iommu = {
	.name = "mdp0",
	.ncb = 2,
};

static struct msm_iommu_dev mdp1_iommu = {
	.name = "mdp1",
	.ncb = 2,
};

static struct msm_iommu_dev rot_iommu = {
	.name = "rot",
	.ncb = 2,
};

static struct msm_iommu_dev ijpeg_iommu = {
	.name = "ijpeg",
	.ncb = 2,
};

static struct msm_iommu_dev vfe_iommu = {
	.name = "vfe",
	.ncb = 2,
};

static struct msm_iommu_dev vcodec_a_iommu = {
	.name = "vcodec_a",
	.ncb = 2,
};

static struct msm_iommu_dev vcodec_b_iommu = {
	.name = "vcodec_b",
	.ncb = 2,
};

static struct msm_iommu_dev gfx3d_iommu = {
	.name = "gfx3d",
	.ncb = 3,
};

static struct msm_iommu_dev gfx2d0_iommu = {
	.name = "gfx2d0",
	.ncb = 2,
};

static struct msm_iommu_dev gfx2d1_iommu = {
	.name = "gfx2d1",
	.ncb = 2,
};

static struct platform_device msm_device_iommu_jpegd = {
	.name = "msm_iommu",
	.id = 0,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_jpegd_resources),
	.resource = msm_iommu_jpegd_resources,
};

static struct platform_device msm_device_iommu_vpe = {
	.name = "msm_iommu",
	.id = 1,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_vpe_resources),
	.resource = msm_iommu_vpe_resources,
};

static struct platform_device msm_device_iommu_mdp0 = {
	.name = "msm_iommu",
	.id = 2,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_mdp0_resources),
	.resource = msm_iommu_mdp0_resources,
};

static struct platform_device msm_device_iommu_mdp1 = {
	.name = "msm_iommu",
	.id = 3,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_mdp1_resources),
	.resource = msm_iommu_mdp1_resources,
};

static struct platform_device msm_device_iommu_rot = {
	.name = "msm_iommu",
	.id = 4,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_rot_resources),
	.resource = msm_iommu_rot_resources,
};

static struct platform_device msm_device_iommu_ijpeg = {
	.name = "msm_iommu",
	.id = 5,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_ijpeg_resources),
	.resource = msm_iommu_ijpeg_resources,
};

static struct platform_device msm_device_iommu_vfe = {
	.name = "msm_iommu",
	.id = 6,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_vfe_resources),
	.resource = msm_iommu_vfe_resources,
};

static struct platform_device msm_device_iommu_vcodec_a = {
	.name = "msm_iommu",
	.id = 7,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_vcodec_a_resources),
	.resource = msm_iommu_vcodec_a_resources,
};

static struct platform_device msm_device_iommu_vcodec_b = {
	.name = "msm_iommu",
	.id = 8,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_vcodec_b_resources),
	.resource = msm_iommu_vcodec_b_resources,
};

static struct platform_device msm_device_iommu_gfx3d = {
	.name = "msm_iommu",
	.id = 9,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_gfx3d_resources),
	.resource = msm_iommu_gfx3d_resources,
};

static struct platform_device msm_device_iommu_gfx2d0 = {
	.name = "msm_iommu",
	.id = 10,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_gfx2d0_resources),
	.resource = msm_iommu_gfx2d0_resources,
};

struct platform_device msm_device_iommu_gfx2d1 = {
	.name = "msm_iommu",
	.id = 11,
	.dev = {
		.parent = &msm_root_iommu_dev.dev,
	},
	.num_resources = ARRAY_SIZE(msm_iommu_gfx2d1_resources),
	.resource = msm_iommu_gfx2d1_resources,
};

static struct msm_iommu_ctx_dev jpegd_src_ctx = {
	.name = "jpegd_src",
	.num = 0,
	.mids = {0, -1}
};

static struct msm_iommu_ctx_dev jpegd_dst_ctx = {
	.name = "jpegd_dst",
	.num = 1,
	.mids = {1, -1}
};

static struct msm_iommu_ctx_dev vpe_src_ctx = {
	.name = "vpe_src",
	.num = 0,
	.mids = {0, -1}
};

static struct msm_iommu_ctx_dev vpe_dst_ctx = {
	.name = "vpe_dst",
	.num = 1,
	.mids = {1, -1}
};

static struct msm_iommu_ctx_dev mdp_vg1_ctx = {
	.name = "mdp_vg1",
	.num = 0,
	.mids = {0, 2, -1}
};

static struct msm_iommu_ctx_dev mdp_rgb1_ctx = {
	.name = "mdp_rgb1",
	.num = 1,
	.mids = {1, 3, 4, 5, 6, 7, 8, 9, 10, -1}
};

static struct msm_iommu_ctx_dev mdp_vg2_ctx = {
	.name = "mdp_vg2",
	.num = 0,
	.mids = {0, 2, -1}
};

static struct msm_iommu_ctx_dev mdp_rgb2_ctx = {
	.name = "mdp_rgb2",
	.num = 1,
	.mids = {1, 3, 4, 5, 6, 7, 8, 9, 10, -1}
};

static struct msm_iommu_ctx_dev rot_src_ctx = {
	.name = "rot_src",
	.num = 0,
	.mids = {0, -1}
};

static struct msm_iommu_ctx_dev rot_dst_ctx = {
	.name = "rot_dst",
	.num = 1,
	.mids = {1, -1}
};

static struct msm_iommu_ctx_dev ijpeg_src_ctx = {
	.name = "ijpeg_src",
	.num = 0,
	.mids = {0, -1}
};

static struct msm_iommu_ctx_dev ijpeg_dst_ctx = {
	.name = "ijpeg_dst",
	.num = 1,
	.mids = {1, -1}
};

static struct msm_iommu_ctx_dev vfe_imgwr_ctx = {
	.name = "vfe_imgwr",
	.num = 0,
	.mids = {2, 3, 4, 5, 6, 7, 8, -1}
};

static struct msm_iommu_ctx_dev vfe_misc_ctx = {
	.name = "vfe_misc",
	.num = 1,
	.mids = {0, 1, 9, -1}
};

static struct msm_iommu_ctx_dev vcodec_a_stream_ctx = {
	.name = "vcodec_a_stream",
	.num = 0,
	.mids = {2, 5, -1}
};

static struct msm_iommu_ctx_dev vcodec_a_mm1_ctx = {
	.name = "vcodec_a_mm1",
	.num = 1,
	.mids = {0, 1, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1}
};

static struct msm_iommu_ctx_dev vcodec_b_mm2_ctx = {
	.name = "vcodec_b_mm2",
	.num = 0,
	.mids = {0, 1, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1}
};

static struct msm_iommu_ctx_dev gfx3d_user_ctx = {
	.name = "gfx3d_user",
	.num = 0,
	.mids = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1}
};

static struct msm_iommu_ctx_dev gfx3d_priv_ctx = {
	.name = "gfx3d_priv",
	.num = 1,
	.mids = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
		 31, -1}
};

static struct msm_iommu_ctx_dev gfx2d0_2d0_ctx = {
	.name = "gfx2d0_2d0",
	.num = 0,
	.mids = {0, 1, 2, 3, 4, 5, 6, 7, -1}
};

static struct msm_iommu_ctx_dev gfx2d1_2d1_ctx = {
	.name = "gfx2d1_2d1",
	.num = 0,
	.mids = {0, 1, 2, 3, 4, 5, 6, 7, -1}
};

static struct platform_device msm_device_jpegd_src_ctx = {
	.name = "msm_iommu_ctx",
	.id = 0,
	.dev = {
		.parent = &msm_device_iommu_jpegd.dev,
	},
};

static struct platform_device msm_device_jpegd_dst_ctx = {
	.name = "msm_iommu_ctx",
	.id = 1,
	.dev = {
		.parent = &msm_device_iommu_jpegd.dev,
	},
};

static struct platform_device msm_device_vpe_src_ctx = {
	.name = "msm_iommu_ctx",
	.id = 2,
	.dev = {
		.parent = &msm_device_iommu_vpe.dev,
	},
};

static struct platform_device msm_device_vpe_dst_ctx = {
	.name = "msm_iommu_ctx",
	.id = 3,
	.dev = {
		.parent = &msm_device_iommu_vpe.dev,
	},
};

static struct platform_device msm_device_mdp_vg1_ctx = {
	.name = "msm_iommu_ctx",
	.id = 4,
	.dev = {
		.parent = &msm_device_iommu_mdp0.dev,
	},
};

static struct platform_device msm_device_mdp_rgb1_ctx = {
	.name = "msm_iommu_ctx",
	.id = 5,
	.dev = {
		.parent = &msm_device_iommu_mdp0.dev,
	},
};

static struct platform_device msm_device_mdp_vg2_ctx = {
	.name = "msm_iommu_ctx",
	.id = 6,
	.dev = {
		.parent = &msm_device_iommu_mdp1.dev,
	},
};

static struct platform_device msm_device_mdp_rgb2_ctx = {
	.name = "msm_iommu_ctx",
	.id = 7,
	.dev = {
		.parent = &msm_device_iommu_mdp1.dev,
	},
};

static struct platform_device msm_device_rot_src_ctx = {
	.name = "msm_iommu_ctx",
	.id = 8,
	.dev = {
		.parent = &msm_device_iommu_rot.dev,
	},
};

static struct platform_device msm_device_rot_dst_ctx = {
	.name = "msm_iommu_ctx",
	.id = 9,
	.dev = {
		.parent = &msm_device_iommu_rot.dev,
	},
};

static struct platform_device msm_device_ijpeg_src_ctx = {
	.name = "msm_iommu_ctx",
	.id = 10,
	.dev = {
		.parent = &msm_device_iommu_ijpeg.dev,
	},
};

static struct platform_device msm_device_ijpeg_dst_ctx = {
	.name = "msm_iommu_ctx",
	.id = 11,
	.dev = {
		.parent = &msm_device_iommu_ijpeg.dev,
	},
};

static struct platform_device msm_device_vfe_imgwr_ctx = {
	.name = "msm_iommu_ctx",
	.id = 12,
	.dev = {
		.parent = &msm_device_iommu_vfe.dev,
	},
};

static struct platform_device msm_device_vfe_misc_ctx = {
	.name = "msm_iommu_ctx",
	.id = 13,
	.dev = {
		.parent = &msm_device_iommu_vfe.dev,
	},
};

static struct platform_device msm_device_vcodec_a_stream_ctx = {
	.name = "msm_iommu_ctx",
	.id = 14,
	.dev = {
		.parent = &msm_device_iommu_vcodec_a.dev,
	},
};

static struct platform_device msm_device_vcodec_a_mm1_ctx = {
	.name = "msm_iommu_ctx",
	.id = 15,
	.dev = {
		.parent = &msm_device_iommu_vcodec_a.dev,
	},
};

static struct platform_device msm_device_vcodec_b_mm2_ctx = {
	.name = "msm_iommu_ctx",
	.id = 16,
	.dev = {
		.parent = &msm_device_iommu_vcodec_b.dev,
	},
};

static struct platform_device msm_device_gfx3d_user_ctx = {
	.name = "msm_iommu_ctx",
	.id = 17,
	.dev = {
		.parent = &msm_device_iommu_gfx3d.dev,
	},
};

static struct platform_device msm_device_gfx3d_priv_ctx = {
	.name = "msm_iommu_ctx",
	.id = 18,
	.dev = {
		.parent = &msm_device_iommu_gfx3d.dev,
	},
};

static struct platform_device msm_device_gfx2d0_2d0_ctx = {
	.name = "msm_iommu_ctx",
	.id = 19,
	.dev = {
		.parent = &msm_device_iommu_gfx2d0.dev,
	},
};

static struct platform_device msm_device_gfx2d1_2d1_ctx = {
	.name = "msm_iommu_ctx",
	.id = 20,
	.dev = {
		.parent = &msm_device_iommu_gfx2d1.dev,
	},
};

static struct platform_device *msm_iommu_devs[] = {
	&msm_device_iommu_jpegd,
	&msm_device_iommu_vpe,
	&msm_device_iommu_mdp0,
	&msm_device_iommu_mdp1,
	&msm_device_iommu_rot,
	&msm_device_iommu_ijpeg,
	&msm_device_iommu_vfe,
	&msm_device_iommu_vcodec_a,
	&msm_device_iommu_vcodec_b,
	&msm_device_iommu_gfx3d,
	&msm_device_iommu_gfx2d0,
	&msm_device_iommu_gfx2d1,
};

static struct msm_iommu_dev *msm_iommu_data[] = {
	&jpegd_iommu,
	&vpe_iommu,
	&mdp0_iommu,
	&mdp1_iommu,
	&rot_iommu,
	&ijpeg_iommu,
	&vfe_iommu,
	&vcodec_a_iommu,
	&vcodec_b_iommu,
	&gfx3d_iommu,
	&gfx2d0_iommu,
	&gfx2d1_iommu,
};

static struct platform_device *msm_iommu_ctx_devs[] = {
	&msm_device_jpegd_src_ctx,
	&msm_device_jpegd_dst_ctx,
	&msm_device_vpe_src_ctx,
	&msm_device_vpe_dst_ctx,
	&msm_device_mdp_vg1_ctx,
	&msm_device_mdp_rgb1_ctx,
	&msm_device_mdp_vg2_ctx,
	&msm_device_mdp_rgb2_ctx,
	&msm_device_rot_src_ctx,
	&msm_device_rot_dst_ctx,
	&msm_device_ijpeg_src_ctx,
	&msm_device_ijpeg_dst_ctx,
	&msm_device_vfe_imgwr_ctx,
	&msm_device_vfe_misc_ctx,
	&msm_device_vcodec_a_stream_ctx,
	&msm_device_vcodec_a_mm1_ctx,
	&msm_device_vcodec_b_mm2_ctx,
	&msm_device_gfx3d_user_ctx,
	&msm_device_gfx3d_priv_ctx,
	&msm_device_gfx2d0_2d0_ctx,
	&msm_device_gfx2d1_2d1_ctx,
};

static struct msm_iommu_ctx_dev *msm_iommu_ctx_data[] = {
	&jpegd_src_ctx,
	&jpegd_dst_ctx,
	&vpe_src_ctx,
	&vpe_dst_ctx,
	&mdp_vg1_ctx,
	&mdp_rgb1_ctx,
	&mdp_vg2_ctx,
	&mdp_rgb2_ctx,
	&rot_src_ctx,
	&rot_dst_ctx,
	&ijpeg_src_ctx,
	&ijpeg_dst_ctx,
	&vfe_imgwr_ctx,
	&vfe_misc_ctx,
	&vcodec_a_stream_ctx,
	&vcodec_a_mm1_ctx,
	&vcodec_b_mm2_ctx,
	&gfx3d_user_ctx,
	&gfx3d_priv_ctx,
	&gfx2d0_2d0_ctx,
	&gfx2d1_2d1_ctx,
};

static int __init msm8x60_iommu_init(void)
{
	int ret, i;

	ret = platform_device_register(&msm_root_iommu_dev);
	if (ret != 0) {
		pr_err("Failed to register root IOMMU device!\n");
		goto failure;
	}

	for (i = 0; i < ARRAY_SIZE(msm_iommu_devs); i++) {
		ret = platform_device_add_data(msm_iommu_devs[i],
					       msm_iommu_data[i],
					       sizeof(struct msm_iommu_dev));
		if (ret != 0) {
			pr_err("platform_device_add_data failed, "
			       "i = %d\n", i);
			goto failure_unwind;
		}

		ret = platform_device_register(msm_iommu_devs[i]);

		if (ret != 0) {
			pr_err("platform_device_register iommu failed, "
			       "i = %d\n", i);
			goto failure_unwind;
		}
	}

	for (i = 0; i < ARRAY_SIZE(msm_iommu_ctx_devs); i++) {
		ret = platform_device_add_data(msm_iommu_ctx_devs[i],
					       msm_iommu_ctx_data[i],
					       sizeof(*msm_iommu_ctx_devs[i]));
		if (ret != 0) {
			pr_err("platform_device_add_data iommu failed, "
			       "i = %d\n", i);
			goto failure_unwind2;
		}

		ret = platform_device_register(msm_iommu_ctx_devs[i]);
		if (ret != 0) {
			pr_err("platform_device_register ctx failed, "
			       "i = %d\n", i);
			goto failure_unwind2;
		}
	}
	return 0;

failure_unwind2:
	while (--i >= 0)
		platform_device_unregister(msm_iommu_ctx_devs[i]);
failure_unwind:
	while (--i >= 0)
		platform_device_unregister(msm_iommu_devs[i]);

	platform_device_unregister(&msm_root_iommu_dev);
failure:
	return ret;
}

static void __exit msm8x60_iommu_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msm_iommu_ctx_devs); i++)
		platform_device_unregister(msm_iommu_ctx_devs[i]);

	for (i = 0; i < ARRAY_SIZE(msm_iommu_devs); ++i)
		platform_device_unregister(msm_iommu_devs[i]);

	platform_device_unregister(&msm_root_iommu_dev);
}

subsys_initcall(msm8x60_iommu_init);
module_exit(msm8x60_iommu_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stepan Moskovchenko <stepanm@codeaurora.org>");
