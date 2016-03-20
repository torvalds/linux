/*
 * IO mappings for OMAP2+
 *
 * IO definitions for TI OMAP processors and boards
 *
 * Copied from arch/arm/mach-sa1100/include/mach/io.h
 * Copyright (C) 1997-1999 Russell King
 *
 * Copyright (C) 2009-2012 Texas Instruments
 * Added OMAP4/5 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define OMAP2_L3_IO_OFFSET	0x90000000
#define OMAP2_L3_IO_ADDRESS(pa)	IOMEM((pa) + OMAP2_L3_IO_OFFSET) /* L3 */

#define OMAP2_L4_IO_OFFSET	0xb2000000
#define OMAP2_L4_IO_ADDRESS(pa)	IOMEM((pa) + OMAP2_L4_IO_OFFSET) /* L4 */

#define OMAP4_L3_IO_OFFSET	0xb4000000
#define OMAP4_L3_IO_ADDRESS(pa)	IOMEM((pa) + OMAP4_L3_IO_OFFSET) /* L3 */

#define AM33XX_L4_WK_IO_OFFSET	0xb5000000
#define AM33XX_L4_WK_IO_ADDRESS(pa)	IOMEM((pa) + AM33XX_L4_WK_IO_OFFSET)

#define OMAP4_L3_PER_IO_OFFSET	0xb1100000
#define OMAP4_L3_PER_IO_ADDRESS(pa)	IOMEM((pa) + OMAP4_L3_PER_IO_OFFSET)

#define OMAP2_EMU_IO_OFFSET		0xaa800000	/* Emulation */
#define OMAP2_EMU_IO_ADDRESS(pa)	IOMEM((pa) + OMAP2_EMU_IO_OFFSET)

/*
 * ----------------------------------------------------------------------------
 * Omap2 specific IO mapping
 * ----------------------------------------------------------------------------
 */

/* We map both L3 and L4 on OMAP2 */
#define L3_24XX_PHYS	L3_24XX_BASE	/* 0x68000000 --> 0xf8000000*/
#define L3_24XX_VIRT	(L3_24XX_PHYS + OMAP2_L3_IO_OFFSET)
#define L3_24XX_SIZE	SZ_1M		/* 44kB of 128MB used, want 1MB sect */
#define L4_24XX_PHYS	L4_24XX_BASE	/* 0x48000000 --> 0xfa000000 */
#define L4_24XX_VIRT	(L4_24XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_24XX_SIZE	SZ_1M		/* 1MB of 128MB used, want 1MB sect */

#define L4_WK_243X_PHYS		L4_WK_243X_BASE	/* 0x49000000 --> 0xfb000000 */
#define L4_WK_243X_VIRT		(L4_WK_243X_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_WK_243X_SIZE		SZ_1M
#define OMAP243X_GPMC_PHYS	OMAP243X_GPMC_BASE
#define OMAP243X_GPMC_VIRT	(OMAP243X_GPMC_PHYS + OMAP2_L3_IO_OFFSET)
						/* 0x6e000000 --> 0xfe000000 */
#define OMAP243X_GPMC_SIZE	SZ_1M
#define OMAP243X_SDRC_PHYS	OMAP243X_SDRC_BASE
						/* 0x6D000000 --> 0xfd000000 */
#define OMAP243X_SDRC_VIRT	(OMAP243X_SDRC_PHYS + OMAP2_L3_IO_OFFSET)
#define OMAP243X_SDRC_SIZE	SZ_1M
#define OMAP243X_SMS_PHYS	OMAP243X_SMS_BASE
						/* 0x6c000000 --> 0xfc000000 */
#define OMAP243X_SMS_VIRT	(OMAP243X_SMS_PHYS + OMAP2_L3_IO_OFFSET)
#define OMAP243X_SMS_SIZE	SZ_1M

/* 2420 IVA */
#define DSP_MEM_2420_PHYS	OMAP2420_DSP_MEM_BASE
						/* 0x58000000 --> 0xfc100000 */
#define DSP_MEM_2420_VIRT	0xfc100000
#define DSP_MEM_2420_SIZE	0x28000
#define DSP_IPI_2420_PHYS	OMAP2420_DSP_IPI_BASE
						/* 0x59000000 --> 0xfc128000 */
#define DSP_IPI_2420_VIRT	0xfc128000
#define DSP_IPI_2420_SIZE	SZ_4K
#define DSP_MMU_2420_PHYS	OMAP2420_DSP_MMU_BASE
						/* 0x5a000000 --> 0xfc129000 */
#define DSP_MMU_2420_VIRT	0xfc129000
#define DSP_MMU_2420_SIZE	SZ_4K

/* 2430 IVA2.1 - currently unmapped */

/*
 * ----------------------------------------------------------------------------
 * Omap3 specific IO mapping
 * ----------------------------------------------------------------------------
 */

/* We map both L3 and L4 on OMAP3 */
#define L3_34XX_PHYS		L3_34XX_BASE	/* 0x68000000 --> 0xf8000000 */
#define L3_34XX_VIRT		(L3_34XX_PHYS + OMAP2_L3_IO_OFFSET)
#define L3_34XX_SIZE		SZ_1M   /* 44kB of 128MB used, want 1MB sect */

#define L4_34XX_PHYS		L4_34XX_BASE	/* 0x48000000 --> 0xfa000000 */
#define L4_34XX_VIRT		(L4_34XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_34XX_SIZE		SZ_4M   /* 1MB of 128MB used, want 1MB sect */

/*
 * ----------------------------------------------------------------------------
 * AM33XX specific IO mapping
 * ----------------------------------------------------------------------------
 */
#define L4_WK_AM33XX_PHYS	L4_WK_AM33XX_BASE
#define L4_WK_AM33XX_VIRT	(L4_WK_AM33XX_PHYS + AM33XX_L4_WK_IO_OFFSET)
#define L4_WK_AM33XX_SIZE	SZ_4M   /* 1MB of 128MB used, want 1MB sect */

/*
 * Need to look at the Size 4M for L4.
 * VPOM3430 was not working for Int controller
 */

#define L4_PER_34XX_PHYS	L4_PER_34XX_BASE
						/* 0x49000000 --> 0xfb000000 */
#define L4_PER_34XX_VIRT	(L4_PER_34XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_PER_34XX_SIZE	SZ_1M

#define L4_EMU_34XX_PHYS	L4_EMU_34XX_BASE
						/* 0x54000000 --> 0xfe800000 */
#define L4_EMU_34XX_VIRT	(L4_EMU_34XX_PHYS + OMAP2_EMU_IO_OFFSET)
#define L4_EMU_34XX_SIZE	SZ_8M

#define OMAP34XX_GPMC_PHYS	OMAP34XX_GPMC_BASE
						/* 0x6e000000 --> 0xfe000000 */
#define OMAP34XX_GPMC_VIRT	(OMAP34XX_GPMC_PHYS + OMAP2_L3_IO_OFFSET)
#define OMAP34XX_GPMC_SIZE	SZ_1M

#define OMAP343X_SMS_PHYS	OMAP343X_SMS_BASE
						/* 0x6c000000 --> 0xfc000000 */
#define OMAP343X_SMS_VIRT	(OMAP343X_SMS_PHYS + OMAP2_L3_IO_OFFSET)
#define OMAP343X_SMS_SIZE	SZ_1M

#define OMAP343X_SDRC_PHYS	OMAP343X_SDRC_BASE
						/* 0x6D000000 --> 0xfd000000 */
#define OMAP343X_SDRC_VIRT	(OMAP343X_SDRC_PHYS + OMAP2_L3_IO_OFFSET)
#define OMAP343X_SDRC_SIZE	SZ_1M

/* 3430 IVA - currently unmapped */

/*
 * ----------------------------------------------------------------------------
 * Omap4 specific IO mapping
 * ----------------------------------------------------------------------------
 */

/* We map both L3 and L4 on OMAP4 */
#define L3_44XX_PHYS		L3_44XX_BASE	/* 0x44000000 --> 0xf8000000 */
#define L3_44XX_VIRT		(L3_44XX_PHYS + OMAP4_L3_IO_OFFSET)
#define L3_44XX_SIZE		SZ_1M

#define L4_44XX_PHYS		L4_44XX_BASE	/* 0x4a000000 --> 0xfc000000 */
#define L4_44XX_VIRT		(L4_44XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_44XX_SIZE		SZ_4M

#define L4_PER_44XX_PHYS	L4_PER_44XX_BASE
						/* 0x48000000 --> 0xfa000000 */
#define L4_PER_44XX_VIRT	(L4_PER_44XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_PER_44XX_SIZE	SZ_4M

#define L4_ABE_44XX_PHYS	L4_ABE_44XX_BASE
						/* 0x49000000 --> 0xfb000000 */
#define L4_ABE_44XX_VIRT	(L4_ABE_44XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_ABE_44XX_SIZE	SZ_1M
/*
 * ----------------------------------------------------------------------------
 * Omap5 specific IO mapping
 * ----------------------------------------------------------------------------
 */
#define L3_54XX_PHYS		L3_54XX_BASE	/* 0x44000000 --> 0xf8000000 */
#define L3_54XX_VIRT		(L3_54XX_PHYS + OMAP4_L3_IO_OFFSET)
#define L3_54XX_SIZE		SZ_1M

#define L4_54XX_PHYS		L4_54XX_BASE	/* 0x4a000000 --> 0xfc000000 */
#define L4_54XX_VIRT		(L4_54XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_54XX_SIZE		SZ_4M

#define L4_WK_54XX_PHYS		L4_WK_54XX_BASE	/* 0x4ae00000 --> 0xfce00000 */
#define L4_WK_54XX_VIRT		(L4_WK_54XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_WK_54XX_SIZE		SZ_2M

#define L4_PER_54XX_PHYS	L4_PER_54XX_BASE /* 0x48000000 --> 0xfa000000 */
#define L4_PER_54XX_VIRT	(L4_PER_54XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_PER_54XX_SIZE	SZ_4M

/*
 * ----------------------------------------------------------------------------
 * DRA7xx specific IO mapping
 * ----------------------------------------------------------------------------
 */
/*
 * L3_MAIN_SN_DRA7XX_PHYS 0x44000000 --> 0xf8000000
 * The overall space is 24MiB (0x4400_0000<->0x457F_FFFF), but mapping
 * everything is just inefficient, since, there are too many address holes.
 */
#define L3_MAIN_SN_DRA7XX_PHYS		L3_MAIN_SN_DRA7XX_BASE
#define L3_MAIN_SN_DRA7XX_VIRT		(L3_MAIN_SN_DRA7XX_PHYS + OMAP4_L3_IO_OFFSET)
#define L3_MAIN_SN_DRA7XX_SIZE		SZ_1M

/*
 * L4_PER1_DRA7XX_PHYS	(0x4800_000<>0x480D_2FFF) -> 0.82MiB (alloc 1MiB)
 *	(0x48000000<->0x48100000) <=> (0xFA000000<->0xFA100000)
 */
#define L4_PER1_DRA7XX_PHYS		L4_PER1_DRA7XX_BASE
#define L4_PER1_DRA7XX_VIRT		(L4_PER1_DRA7XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_PER1_DRA7XX_SIZE		SZ_1M

/*
 * L4_CFG_MPU_DRA7XX_PHYS	(0x48210000<>0x482A_F2FF) -> 0.62MiB (alloc 1MiB)
 *	(0x48210000<->0x48310000) <=> (0xFA210000<->0xFA310000)
 * NOTE: This is a bit of an orphan memory map sitting isolated in TRM
 */
#define L4_CFG_MPU_DRA7XX_PHYS		L4_CFG_MPU_DRA7XX_BASE
#define L4_CFG_MPU_DRA7XX_VIRT		(L4_CFG_MPU_DRA7XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_CFG_MPU_DRA7XX_SIZE		SZ_1M

/*
 * L4_PER2_DRA7XX_PHYS	(0x4840_0000<>0x4848_8FFF) -> .53MiB (alloc 1MiB)
 *	(0x48400000<->0x48500000) <=> (0xFA400000<->0xFA500000)
 */
#define L4_PER2_DRA7XX_PHYS		L4_PER2_DRA7XX_BASE
#define L4_PER2_DRA7XX_VIRT		(L4_PER2_DRA7XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_PER2_DRA7XX_SIZE		SZ_1M

/*
 * L4_PER3_DRA7XX_PHYS	(0x4880_0000<>0x489E_0FFF) -> 1.87MiB (alloc 2MiB)
 *	(0x48800000<->0x48A00000) <=> (0xFA800000<->0xFAA00000)
 */
#define L4_PER3_DRA7XX_PHYS		L4_PER3_DRA7XX_BASE
#define L4_PER3_DRA7XX_VIRT		(L4_PER3_DRA7XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_PER3_DRA7XX_SIZE		SZ_2M

/*
 * L4_CFG_DRA7XX_PHYS	(0x4A00_0000<>0x4A22_BFFF) ->2.17MiB (alloc 3MiB)?
 *	(0x4A000000<->0x4A300000) <=> (0xFC000000<->0xFC300000)
 */
#define L4_CFG_DRA7XX_PHYS		L4_CFG_DRA7XX_BASE
#define L4_CFG_DRA7XX_VIRT		(L4_CFG_DRA7XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_CFG_DRA7XX_SIZE		(SZ_1M + SZ_2M)

/*
 * L4_WKUP_DRA7XX_PHYS	(0x4AE0_0000<>0x4AE3_EFFF) -> .24 mb (alloc 1MiB)?
 *	(0x4AE00000<->4AF00000)	<=> (0xFCE00000<->0xFCF00000)
 */
#define L4_WKUP_DRA7XX_PHYS		L4_WKUP_DRA7XX_BASE
#define L4_WKUP_DRA7XX_VIRT		(L4_WKUP_DRA7XX_PHYS + OMAP2_L4_IO_OFFSET)
#define L4_WKUP_DRA7XX_SIZE		SZ_1M
