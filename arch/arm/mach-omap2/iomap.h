/*
 * IO mappings for OMAP2+
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

#ifdef __ASSEMBLER__
#define IOMEM(x)		(x)
#else
#define IOMEM(x)		((void __force __iomem *)(x))
#endif

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

#define OMAP4_GPMC_IO_OFFSET		0xa9000000
#define OMAP4_GPMC_IO_ADDRESS(pa)	IOMEM((pa) + OMAP4_GPMC_IO_OFFSET)

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

#define L4_EMU_44XX_PHYS	L4_EMU_44XX_BASE
						/* 0x54000000 --> 0xfe800000 */
#define L4_EMU_44XX_VIRT	(L4_EMU_44XX_PHYS + OMAP2_EMU_IO_OFFSET)
#define L4_EMU_44XX_SIZE	SZ_8M

#define OMAP44XX_GPMC_PHYS	OMAP44XX_GPMC_BASE
						/* 0x50000000 --> 0xf9000000 */
#define OMAP44XX_GPMC_VIRT	(OMAP44XX_GPMC_PHYS + OMAP4_GPMC_IO_OFFSET)
#define OMAP44XX_GPMC_SIZE	SZ_1M


#define OMAP44XX_EMIF1_PHYS	OMAP44XX_EMIF1_BASE
						/* 0x4c000000 --> 0xfd100000 */
#define OMAP44XX_EMIF1_VIRT	(OMAP44XX_EMIF1_PHYS + OMAP4_L3_PER_IO_OFFSET)
#define OMAP44XX_EMIF1_SIZE	SZ_1M

#define OMAP44XX_EMIF2_PHYS	OMAP44XX_EMIF2_BASE
						/* 0x4d000000 --> 0xfd200000 */
#define OMAP44XX_EMIF2_SIZE	SZ_1M
#define OMAP44XX_EMIF2_VIRT	(OMAP44XX_EMIF1_VIRT + OMAP44XX_EMIF1_SIZE)

#define OMAP44XX_DMM_PHYS	OMAP44XX_DMM_BASE
						/* 0x4e000000 --> 0xfd300000 */
#define OMAP44XX_DMM_SIZE	SZ_1M
#define OMAP44XX_DMM_VIRT	(OMAP44XX_EMIF2_VIRT + OMAP44XX_EMIF2_SIZE)
