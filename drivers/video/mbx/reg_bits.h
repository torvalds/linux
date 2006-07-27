#ifndef __REG_BITS_2700G_
#define __REG_BITS_2700G_

/* use defines from asm-arm/arch-pxa/bitfields.h for bit fields access */
#define UData(Data)	((unsigned long) (Data))
#define Fld(Size, Shft)	(((Size) << 16) + (Shft))
#define FSize(Field)	((Field) >> 16)
#define FShft(Field)	((Field) & 0x0000FFFF)
#define FMsk(Field)	(((UData (1) << FSize (Field)) - 1) << FShft (Field))
#define FAlnMsk(Field)	((UData (1) << FSize (Field)) - 1)
#define F1stBit(Field)	(UData (1) << FShft (Field))

#define SYSRST_RST	(1 << 0)

/* SYSCLKSRC - SYSCLK Source Control Register */
#define SYSCLKSRC_SEL	Fld(2,0)
#define SYSCLKSRC_REF	((0x0) << FShft(SYSCLKSRC_SEL))
#define SYSCLKSRC_PLL_1	((0x1) << FShft(SYSCLKSRC_SEL))
#define SYSCLKSRC_PLL_2	((0x2) << FShft(SYSCLKSRC_SEL))

/* PIXCLKSRC - PIXCLK Source Control Register */
#define PIXCLKSRC_SEL	Fld(2,0)
#define PIXCLKSRC_REF	((0x0) << FShft(PIXCLKSRC_SEL))
#define PIXCLKSRC_PLL_1	((0x1) << FShft(PIXCLKSRC_SEL))
#define PIXCLKSRC_PLL_2	((0x2) << FShft(PIXCLKSRC_SEL))

/* Clock Disable Register */
#define CLKSLEEP_SLP	(1 << 0)

/* Core PLL Control Register */
#define CORE_PLL_M	Fld(6,7)
#define Core_Pll_M(x)	((x) << FShft(CORE_PLL_M))
#define CORE_PLL_N	Fld(3,4)
#define Core_Pll_N(x)	((x) << FShft(CORE_PLL_N))
#define CORE_PLL_P	Fld(3,1)
#define Core_Pll_P(x)	((x) << FShft(CORE_PLL_P))
#define CORE_PLL_EN	(1 << 0)

/* Display PLL Control Register */
#define DISP_PLL_M	Fld(6,7)
#define Disp_Pll_M(x)	((x) << FShft(DISP_PLL_M))
#define DISP_PLL_N	Fld(3,4)
#define Disp_Pll_N(x)	((x) << FShft(DISP_PLL_N))
#define DISP_PLL_P	Fld(3,1)
#define Disp_Pll_P(x)	((x) << FShft(DISP_PLL_P))
#define DISP_PLL_EN	(1 << 0)

/* PLL status register */
#define PLLSTAT_CORE_PLL_LOST_L	(1 << 3)
#define PLLSTAT_CORE_PLL_LSTS	(1 << 2)
#define PLLSTAT_DISP_PLL_LOST_L	(1 << 1)
#define PLLSTAT_DISP_PLL_LSTS	(1 << 0)

/* Video and scale clock control register */
#define VOVRCLK_EN	(1 << 0)

/* Pixel clock control register */
#define PIXCLK_EN	(1 << 0)

/* Memory clock control register */
#define MEMCLK_EN	(1 << 0)

/* MBX clock control register */
#define MBXCLK_DIV	Fld(2,2)
#define MBXCLK_DIV_1	((0x0) << FShft(MBXCLK_DIV))
#define MBXCLK_DIV_2	((0x1) << FShft(MBXCLK_DIV))
#define MBXCLK_DIV_3	((0x2) << FShft(MBXCLK_DIV))
#define MBXCLK_DIV_4	((0x3) << FShft(MBXCLK_DIV))
#define MBXCLK_EN	Fld(2,0)
#define MBXCLK_EN_NONE	((0x0) << FShft(MBXCLK_EN))
#define MBXCLK_EN_2D	((0x1) << FShft(MBXCLK_EN))
#define MBXCLK_EN_BOTH	((0x2) << FShft(MBXCLK_EN))

/* M24 clock control register */
#define M24CLK_DIV	Fld(2,1)
#define M24CLK_DIV_1	((0x0) << FShft(M24CLK_DIV))
#define M24CLK_DIV_2	((0x1) << FShft(M24CLK_DIV))
#define M24CLK_DIV_3	((0x2) << FShft(M24CLK_DIV))
#define M24CLK_DIV_4	((0x3) << FShft(M24CLK_DIV))
#define M24CLK_EN	(1 << 0)

/* SDRAM clock control register */
#define SDCLK_EN	(1 << 0)

/* PixClk Divisor Register */
#define PIXCLKDIV_PD	Fld(9,0)
#define Pixclkdiv_Pd(x)	((x) << FShft(PIXCLKDIV_PD))

/* LCD Config control register */
#define LCDCFG_IN_FMT	Fld(3,28)
#define Lcdcfg_In_Fmt(x)	((x) << FShft(LCDCFG_IN_FMT))
#define LCDCFG_LCD1DEN_POL	(1 << 27)
#define LCDCFG_LCD1FCLK_POL	(1 << 26)
#define LCDCFG_LCD1LCLK_POL	(1 << 25)
#define LCDCFG_LCD1D_POL	(1 << 24)
#define LCDCFG_LCD2DEN_POL	(1 << 23)
#define LCDCFG_LCD2FCLK_POL	(1 << 22)
#define LCDCFG_LCD2LCLK_POL	(1 << 21)
#define LCDCFG_LCD2D_POL	(1 << 20)
#define LCDCFG_LCD1_TS		(1 << 19)
#define LCDCFG_LCD1D_DS		(1 << 18)
#define LCDCFG_LCD1C_DS		(1 << 17)
#define LCDCFG_LCD1_IS_IN	(1 << 16)
#define LCDCFG_LCD2_TS		(1 << 3)
#define LCDCFG_LCD2D_DS		(1 << 2)
#define LCDCFG_LCD2C_DS		(1 << 1)
#define LCDCFG_LCD2_IS_IN	(1 << 0)

/* On-Die Frame Buffer Power Control Register */
#define ODFBPWR_SLOW	(1 << 2)
#define ODFBPWR_MODE	Fld(2,0)
#define ODFBPWR_MODE_ACT	((0x0) << FShft(ODFBPWR_MODE))
#define ODFBPWR_MODE_ACT_LP	((0x1) << FShft(ODFBPWR_MODE))
#define ODFBPWR_MODE_SLEEP	((0x2) << FShft(ODFBPWR_MODE))
#define ODFBPWR_MODE_SHUTD	((0x3) << FShft(ODFBPWR_MODE))

/* On-Die Frame Buffer Power State Status Register */
#define ODFBSTAT_ACT	(1 << 2)
#define ODFBSTAT_SLP	(1 << 1)
#define ODFBSTAT_SDN	(1 << 0)

/* LMRST - Local Memory (SDRAM) Reset */
#define LMRST_MC_RST	(1 << 0)

/* LMCFG - Local Memory (SDRAM) Configuration Register */
#define LMCFG_LMC_DS	(1 << 5)
#define LMCFG_LMD_DS	(1 << 4)
#define LMCFG_LMA_DS	(1 << 3)
#define LMCFG_LMC_TS	(1 << 2)
#define LMCFG_LMD_TS	(1 << 1)
#define LMCFG_LMA_TS	(1 << 0)

/* LMPWR - Local Memory (SDRAM) Power Control Register */
#define LMPWR_MC_PWR_CNT	Fld(2,0)
#define LMPWR_MC_PWR_ACT	((0x0) << FShft(LMPWR_MC_PWR_CNT)) /* Active */
#define LMPWR_MC_PWR_SRM	((0x1) << FShft(LMPWR_MC_PWR_CNT)) /* Self-refresh */
#define LMPWR_MC_PWR_DPD	((0x3) << FShft(LMPWR_MC_PWR_CNT)) /* deep power down */

/* LMPWRSTAT - Local Memory (SDRAM) Power Status Register */
#define LMPWRSTAT_MC_PWR_CNT	Fld(2,0)
#define LMPWRSTAT_MC_PWR_ACT	((0x0) << FShft(LMPWRSTAT_MC_PWR_CNT)) /* Active */
#define LMPWRSTAT_MC_PWR_SRM	((0x1) << FShft(LMPWRSTAT_MC_PWR_CNT)) /* Self-refresh */
#define LMPWRSTAT_MC_PWR_DPD	((0x3) << FShft(LMPWRSTAT_MC_PWR_CNT)) /* deep power down */

/* LMTYPE - Local Memory (SDRAM) Type Register */
#define LMTYPE_CASLAT	Fld(3,10)
#define LMTYPE_CASLAT_1	((0x1) << FShft(LMTYPE_CASLAT))
#define LMTYPE_CASLAT_2	((0x2) << FShft(LMTYPE_CASLAT))
#define LMTYPE_CASLAT_3	((0x3) << FShft(LMTYPE_CASLAT))
#define LMTYPE_BKSZ	Fld(2,8)
#define LMTYPE_BKSZ_1	((0x1) << FShft(LMTYPE_BKSZ))
#define LMTYPE_BKSZ_2	((0x2) << FShft(LMTYPE_BKSZ))
#define LMTYPE_ROWSZ	Fld(4,4)
#define LMTYPE_ROWSZ_11	((0xb) << FShft(LMTYPE_ROWSZ))
#define LMTYPE_ROWSZ_12	((0xc) << FShft(LMTYPE_ROWSZ))
#define LMTYPE_ROWSZ_13	((0xd) << FShft(LMTYPE_ROWSZ))
#define LMTYPE_COLSZ	Fld(4,0)
#define LMTYPE_COLSZ_7	((0x7) << FShft(LMTYPE_COLSZ))
#define LMTYPE_COLSZ_8	((0x8) << FShft(LMTYPE_COLSZ))
#define LMTYPE_COLSZ_9	((0x9) << FShft(LMTYPE_COLSZ))
#define LMTYPE_COLSZ_10	((0xa) << FShft(LMTYPE_COLSZ))
#define LMTYPE_COLSZ_11	((0xb) << FShft(LMTYPE_COLSZ))
#define LMTYPE_COLSZ_12	((0xc) << FShft(LMTYPE_COLSZ))

/* LMTIM - Local Memory (SDRAM) Timing Register */
#define LMTIM_TRAS	Fld(4,16)
#define Lmtim_Tras(x)	((x) << FShft(LMTIM_TRAS))
#define LMTIM_TRP	Fld(4,12)
#define Lmtim_Trp(x)	((x) << FShft(LMTIM_TRP))
#define LMTIM_TRCD	Fld(4,8)
#define Lmtim_Trcd(x)	((x) << FShft(LMTIM_TRCD))
#define LMTIM_TRC	Fld(4,4)
#define Lmtim_Trc(x)	((x) << FShft(LMTIM_TRC))
#define LMTIM_TDPL	Fld(4,0)
#define Lmtim_Tdpl(x)	((x) << FShft(LMTIM_TDPL))

/* LMREFRESH - Local Memory (SDRAM) tREF Control Register */
#define LMREFRESH_TREF	Fld(2,0)
#define Lmrefresh_Tref(x)	((x) << FShft(LMREFRESH_TREF))

/* GSCTRL - Graphics surface control register */
#define GSCTRL_LUT_EN	(1 << 31)
#define GSCTRL_GPIXFMT	Fld(4,27)
#define GSCTRL_GPIXFMT_INDEXED	((0x0) << FShft(GSCTRL_GPIXFMT))
#define GSCTRL_GPIXFMT_ARGB4444	((0x4) << FShft(GSCTRL_GPIXFMT))
#define GSCTRL_GPIXFMT_ARGB1555	((0x5) << FShft(GSCTRL_GPIXFMT))
#define GSCTRL_GPIXFMT_RGB888	((0x6) << FShft(GSCTRL_GPIXFMT))
#define GSCTRL_GPIXFMT_RGB565	((0x7) << FShft(GSCTRL_GPIXFMT))
#define GSCTRL_GPIXFMT_ARGB8888	((0x8) << FShft(GSCTRL_GPIXFMT))
#define GSCTRL_GAMMA_EN	(1 << 26)

#define GSCTRL_GSWIDTH Fld(11,11)
#define Gsctrl_Width(Pixel)	/* Display Width [1..2048 pix.]  */ \
                        (((Pixel) - 1) << FShft(GSCTRL_GSWIDTH))

#define GSCTRL_GSHEIGHT Fld(11,0)
#define Gsctrl_Height(Pixel)	/* Display Height [1..2048 pix.]  */ \
                        (((Pixel) - 1) << FShft(GSCTRL_GSHEIGHT))

/* GBBASE fileds */
#define GBBASE_GLALPHA Fld(8,24)
#define Gbbase_Glalpha(x)	((x) << FShft(GBBASE_GLALPHA))

#define GBBASE_COLKEY Fld(24,0)
#define Gbbase_Colkey(x)	((x) << FShft(GBBASE_COLKEY))

/* GDRCTRL fields */
#define GDRCTRL_PIXDBL	(1 << 31)
#define GDRCTRL_PIXHLV	(1 << 30)
#define GDRCTRL_LNDBL	(1 << 29)
#define GDRCTRL_LNHLV	(1 << 28)
#define GDRCTRL_COLKEYM	Fld(24,0)
#define Gdrctrl_Colkeym(x)	((x) << FShft(GDRCTRL_COLKEYM))

/* GSCADR graphics stream control address register fields */
#define GSCADR_STR_EN	(1 << 31)
#define GSCADR_COLKEY_EN	(1 << 30)
#define GSCADR_COLKEYSCR	(1 << 29)
#define GSCADR_BLEND_M	Fld(2,27)
#define GSCADR_BLEND_NONE	((0x0) << FShft(GSCADR_BLEND_M))
#define GSCADR_BLEND_INV	((0x1) << FShft(GSCADR_BLEND_M))
#define GSCADR_BLEND_GLOB	((0x2) << FShft(GSCADR_BLEND_M))
#define GSCADR_BLEND_PIX	((0x3) << FShft(GSCADR_BLEND_M))
#define GSCADR_BLEND_POS	Fld(2,24)
#define GSCADR_BLEND_GFX	((0x0) << FShft(GSCADR_BLEND_POS))
#define GSCADR_BLEND_VID	((0x1) << FShft(GSCADR_BLEND_POS))
#define GSCADR_BLEND_CUR	((0x2) << FShft(GSCADR_BLEND_POS))
#define GSCADR_GBASE_ADR	Fld(23,0)
#define Gscadr_Gbase_Adr(x)	((x) << FShft(GSCADR_GBASE_ADR))

/* GSADR graphics stride address register fields */
#define GSADR_SRCSTRIDE	Fld(10,22)
#define Gsadr_Srcstride(x)	((x) << FShft(GSADR_SRCSTRIDE))
#define GSADR_XSTART	Fld(11,11)
#define Gsadr_Xstart(x)		((x) << FShft(GSADR_XSTART))
#define GSADR_YSTART	Fld(11,0)
#define Gsadr_Ystart(y)		((y) << FShft(GSADR_YSTART))

/* GPLUT graphics palette register fields */
#define GPLUT_LUTADR	Fld(8,24)
#define Gplut_Lutadr(x)	((x) << FShft(GPLUT_LUTADR))
#define GPLUT_LUTDATA	Fld(24,0)
#define Gplut_Lutdata(x)	((x) << FShft(GPLUT_LUTDATA))

/* HCCTRL - Hardware Cursor Register fields */
#define HCCTRL_CUR_EN	(1 << 31)
#define HCCTRL_COLKEY_EN	(1 << 29)
#define HCCTRL_COLKEYSRC	(1 << 28)
#define HCCTRL_BLEND_M	Fld(2,26)
#define HCCTRL_BLEND_NONE	((0x0) << FShft(HCCTRL_BLEND_M))
#define HCCTRL_BLEND_INV	((0x1) << FShft(HCCTRL_BLEND_M))
#define HCCTRL_BLEND_GLOB	((0x2) << FShft(HCCTRL_BLEND_M))
#define HCCTRL_BLEND_PIX	((0x3) << FShft(HCCTRL_BLEND_M))
#define HCCTRL_CPIXFMT	Fld(3,23)
#define HCCTRL_CPIXFMT_RGB332	((0x3) << FShft(HCCTRL_CPIXFMT))
#define HCCTRL_CPIXFMT_ARGB4444	((0x4) << FShft(HCCTRL_CPIXFMT))
#define HCCTRL_CPIXFMT_ARGB1555	((0x5) << FShft(HCCTRL_CPIXFMT))
#define HCCTRL_CBASE_ADR	Fld(23,0)
#define Hcctrl_Cbase_Adr(x)	((x) << FShft(HCCTRL_CBASE_ADR))

/* HCSIZE Hardware Cursor Size Register fields */
#define HCSIZE_BLEND_POS	Fld(2,29)
#define HCSIZE_BLEND_GFX	((0x0) << FShft(HCSIZE_BLEND_POS))
#define HCSIZE_BLEND_VID	((0x1) << FShft(HCSIZE_BLEND_POS))
#define HCSIZE_BLEND_CUR	((0x2) << FShft(HCSIZE_BLEND_POS))
#define HCSIZE_CWIDTH	Fld(3,16)
#define Hcsize_Cwidth(x)	((x) << FShft(HCSIZE_CWIDTH))
#define HCSIZE_CHEIGHT	Fld(3,0)
#define Hcsize_Cheight(x)	((x) << FShft(HCSIZE_CHEIGHT))

/* HCPOS Hardware Cursor Position Register fields */
#define HCPOS_SWITCHSRC	(1 << 30)
#define HCPOS_CURBLINK	Fld(6,24)
#define Hcpos_Curblink(x)	((x) << FShft(HCPOS_CURBLINK))
#define HCPOS_XSTART	Fld(12,12)
#define Hcpos_Xstart(x)	((x) << FShft(HCPOS_XSTART))
#define HCPOS_YSTART	Fld(12,0)
#define Hcpos_Ystart(y)	((y) << FShft(HCPOS_YSTART))

/* HCBADR Hardware Cursor Blend Address Register */
#define HCBADR_GLALPHA	Fld(8,24)
#define Hcbadr_Glalpha(x)	((x) << FShft(HCBADR_GLALPHA))
#define HCBADR_COLKEY	Fld(24,0)
#define Hcbadr_Colkey(x)	((x) << FShft(HCBADR_COLKEY))

/* HCCKMSK - Hardware Cursor Color Key Mask Register */
#define HCCKMSK_COLKEY_M	Fld(24,0)
#define Hcckmsk_Colkey_M(x)	((x) << FShft(HCCKMSK_COLKEY_M))

/* DSCTRL - Display sync control register */
#define DSCTRL_SYNCGEN_EN	(1 << 31)
#define DSCTRL_DPL_RST		(1 << 29)
#define DSCTRL_PWRDN_M		(1 << 28)
#define DSCTRL_UPDSYNCCNT	(1 << 26)
#define DSCTRL_UPDINTCNT	(1 << 25)
#define DSCTRL_UPDCNT		(1 << 24)
#define DSCTRL_UPDWAIT	Fld(4,16)
#define Dsctrl_Updwait(x)	((x) << FShft(DSCTRL_UPDWAIT))
#define DSCTRL_CLKPOL		(1 << 11)
#define DSCTRL_CSYNC_EN		(1 << 10)
#define DSCTRL_VS_SLAVE		(1 << 7)
#define DSCTRL_HS_SLAVE		(1 << 6)
#define DSCTRL_BLNK_POL		(1 << 5)
#define DSCTRL_BLNK_DIS		(1 << 4)
#define DSCTRL_VS_POL		(1 << 3)
#define DSCTRL_VS_DIS		(1 << 2)
#define DSCTRL_HS_POL		(1 << 1)
#define DSCTRL_HS_DIS		(1 << 0)

/* DHT01 - Display horizontal timing register 01 */
#define DHT01_HBPS	Fld(12,16)
#define Dht01_Hbps(x)	((x) << FShft(DHT01_HBPS))
#define DHT01_HT	Fld(12,0)
#define Dht01_Ht(x)	((x) << FShft(DHT01_HT))

/* DHT02 - Display horizontal timing register 02 */
#define DHT02_HAS	Fld(12,16)
#define Dht02_Has(x)	((x) << FShft(DHT02_HAS))
#define DHT02_HLBS	Fld(12,0)
#define Dht02_Hlbs(x)	((x) << FShft(DHT02_HLBS))

/* DHT03 - Display horizontal timing register 03 */
#define DHT03_HFPS	Fld(12,16)
#define Dht03_Hfps(x)	((x) << FShft(DHT03_HFPS))
#define DHT03_HRBS	Fld(12,0)
#define Dht03_Hrbs(x)	((x) << FShft(DHT03_HRBS))

/* DVT01 - Display vertical timing register 01 */
#define DVT01_VBPS	Fld(12,16)
#define Dvt01_Vbps(x)	((x) << FShft(DVT01_VBPS))
#define DVT01_VT	Fld(12,0)
#define Dvt01_Vt(x)	((x) << FShft(DVT01_VT))

/* DVT02 - Display vertical timing register 02 */
#define DVT02_VAS	Fld(12,16)
#define Dvt02_Vas(x)	((x) << FShft(DVT02_VAS))
#define DVT02_VTBS	Fld(12,0)
#define Dvt02_Vtbs(x)	((x) << FShft(DVT02_VTBS))

/* DVT03 - Display vertical timing register 03 */
#define DVT03_VFPS	Fld(12,16)
#define Dvt03_Vfps(x)	((x) << FShft(DVT03_VFPS))
#define DVT03_VBBS	Fld(12,0)
#define Dvt03_Vbbs(x)	((x) << FShft(DVT03_VBBS))

/* DVECTRL - display vertical event control register */
#define DVECTRL_VEVENT	Fld(12,16)
#define Dvectrl_Vevent(x)	((x) << FShft(DVECTRL_VEVENT))
#define DVECTRL_VFETCH	Fld(12,0)
#define Dvectrl_Vfetch(x)	((x) << FShft(DVECTRL_VFETCH))

/* DHDET - display horizontal DE timing register */
#define DHDET_HDES	Fld(12,16)
#define Dhdet_Hdes(x)	((x) << FShft(DHDET_HDES))
#define DHDET_HDEF	Fld(12,0)
#define Dhdet_Hdef(x)	((x) << FShft(DHDET_HDEF))

/* DVDET - display vertical DE timing register */
#define DVDET_VDES	Fld(12,16)
#define Dvdet_Vdes(x)	((x) << FShft(DVDET_VDES))
#define DVDET_VDEF	Fld(12,0)
#define Dvdet_Vdef(x)	((x) << FShft(DVDET_VDEF))

/* DODMSK - display output data mask register */
#define DODMSK_MASK_LVL	(1 << 31)
#define DODMSK_BLNK_LVL	(1 << 30)
#define DODMSK_MASK_B	Fld(8,16)
#define Dodmsk_Mask_B(x)	((x) << FShft(DODMSK_MASK_B))
#define DODMSK_MASK_G	Fld(8,8)
#define Dodmsk_Mask_G(x)	((x) << FShft(DODMSK_MASK_G))
#define DODMSK_MASK_R	Fld(8,0)
#define Dodmsk_Mask_R(x)	((x) << FShft(DODMSK_MASK_R))

/* DBCOL - display border color control register */
#define DBCOL_BORDCOL	Fld(24,0)
#define Dbcol_Bordcol(x)	((x) << FShft(DBCOL_BORDCOL))

/* DVLNUM - display vertical line number register */
#define DVLNUM_VLINE	Fld(12,0)
#define Dvlnum_Vline(x)	((x) << FShft(DVLNUM_VLINE))

/* DMCTRL - Display Memory Control Register */
#define DMCTRL_MEM_REF	Fld(2,30)
#define DMCTRL_MEM_REF_ACT	((0x0) << FShft(DMCTRL_MEM_REF))
#define DMCTRL_MEM_REF_HB	((0x1) << FShft(DMCTRL_MEM_REF))
#define DMCTRL_MEM_REF_VB	((0x2) << FShft(DMCTRL_MEM_REF))
#define DMCTRL_MEM_REF_BOTH	((0x3) << FShft(DMCTRL_MEM_REF))
#define DMCTRL_UV_THRHLD	Fld(6,24)
#define Dmctrl_Uv_Thrhld(x)	((x) << FShft(DMCTRL_UV_THRHLD))
#define DMCTRL_V_THRHLD		Fld(7,16)
#define Dmctrl_V_Thrhld(x)	((x) << FShft(DMCTRL_V_THRHLD))
#define DMCTRL_D_THRHLD		Fld(7,8)
#define Dmctrl_D_Thrhld(x)	((x) << FShft(DMCTRL_D_THRHLD))
#define DMCTRL_BURSTLEN	Fld(6,0)
#define Dmctrl_Burstlen(x)	((x) << FShft(DMCTRL_BURSTLEN))


/* DLSTS - display load status register */
#define DLSTS_RLD_ADONE	(1 << 23)
/* #define DLSTS_RLD_ADOUT	Fld(23,0) */

/* DLLCTRL - display list load control register */
#define DLLCTRL_RLD_ADRLN	Fld(8,24)
#define Dllctrl_Rld_Adrln(x)	((x) << FShft(DLLCTRL_RLD_ADRLN))

/* SPOCTRL - Scale Pitch/Order Control Register */
#define SPOCTRL_H_SC_BP		(1 << 31)
#define SPOCTRL_V_SC_BP		(1 << 30)
#define SPOCTRL_HV_SC_OR	(1 << 29)
#define SPOCTRL_VS_UR_C		(1 << 27)
#define SPOCTRL_VORDER	Fld(2,16)
#define SPOCTRL_VORDER_1TAP	((0x0) << FShft(SPOCTRL_VORDER))
#define SPOCTRL_VORDER_2TAP	((0x1) << FShft(SPOCTRL_VORDER))
#define SPOCTRL_VORDER_4TAP	((0x3) << FShft(SPOCTRL_VORDER))
#define SPOCTRL_VPITCH	Fld(16,0)
#define Spoctrl_Vpitch(x)	((x) << FShft(SPOCTRL_VPITCH))

#endif /* __REG_BITS_2700G_ */
