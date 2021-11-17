/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __RTL8712_SYSCFG_BITDEF_H__
#define __RTL8712_SYSCFG_BITDEF_H__

/*SYS_PWR_CTRL*/
/*SRCTRL0*/
/*SRCTRL1*/
/*SYS_CLKR*/

/*SYS_IOS_CTRL*/
#define iso_LDR2RP_SHT		8 /* EE Loader to Retention Path*/
#define iso_LDR2RP		BIT(iso_LDR2RP_SHT) /* 1:isolation, 0:attach*/

/*SYS_CTRL*/
#define FEN_DIO_SDIO_SHT	0
#define FEN_DIO_SDIO		BIT(FEN_DIO_SDIO_SHT)
#define FEN_SDIO_SHT		1
#define FEN_SDIO		BIT(FEN_SDIO_SHT)
#define FEN_USBA_SHT		2
#define FEN_USBA		BIT(FEN_USBA_SHT)
#define FEN_UPLL_SHT		3
#define FEN_UPLL		BIT(FEN_UPLL_SHT)
#define FEN_USBD_SHT		4
#define FEN_USBD		BIT(FEN_USBD_SHT)
#define FEN_DIO_PCIE_SHT	5
#define FEN_DIO_PCIE		BIT(FEN_DIO_PCIE_SHT)
#define FEN_PCIEA_SHT		6
#define FEN_PCIEA		BIT(FEN_PCIEA_SHT)
#define FEN_PPLL_SHT		7
#define FEN_PPLL		BIT(FEN_PPLL_SHT)
#define FEN_PCIED_SHT		8
#define FEN_PCIED		BIT(FEN_PCIED_SHT)
#define FEN_CPUEN_SHT		10
#define FEN_CPUEN		BIT(FEN_CPUEN_SHT)
#define FEN_DCORE_SHT		11
#define FEN_DCORE		BIT(FEN_DCORE_SHT)
#define FEN_ELDR_SHT		12
#define FEN_ELDR		BIT(FEN_ELDR_SHT)
#define PWC_DV2LDR_SHT		13
#define PWC_DV2LDR		BIT(PWC_DV2LDR_SHT) /* Loader Power Enable*/

/*=== SYS_CLKR ===*/
#define SYS_CLKSEL_SHT		0
#define SYS_CLKSEL		BIT(SYS_CLKSEL_SHT) /* System Clock 80MHz*/
#define PS_CLKSEL_SHT		1
#define PS_CLKSEL		BIT(PS_CLKSEL_SHT) /*System power save
						    * clock select.
						    */
#define CPU_CLKSEL_SHT		2
#define CPU_CLKSEL		BIT(CPU_CLKSEL_SHT) /* System Clock select,
						     * 1: AFE source,
						     * 0: System clock(L-Bus)
						     */
#define INT32K_EN_SHT		3
#define INT32K_EN		BIT(INT32K_EN_SHT)
#define MACSLP_SHT		4
#define MACSLP			BIT(MACSLP_SHT)
#define MAC_CLK_EN_SHT		11
#define MAC_CLK_EN		BIT(MAC_CLK_EN_SHT) /* MAC Clock Enable.*/
#define SYS_CLK_EN_SHT		12
#define SYS_CLK_EN		BIT(SYS_CLK_EN_SHT)
#define RING_CLK_EN_SHT		13
#define RING_CLK_EN		BIT(RING_CLK_EN_SHT)
#define SWHW_SEL_SHT		14
#define SWHW_SEL		BIT(SWHW_SEL_SHT) /* Load done,
						   * control path switch.
						   */
#define FWHW_SEL_SHT		15
#define FWHW_SEL		BIT(FWHW_SEL_SHT) /* Sleep exit,
						   * control path switch.
						   */

/*9346CR*/
#define	_VPDIDX_MSK		0xFF00
#define	_VPDIDX_SHT		8
#define	_EEM_MSK		0x00C0
#define	_EEM_SHT		6
#define	_EEM0			BIT(6)
#define	_EEM1			BIT(7)
#define	_EEPROM_EN		BIT(5)
#define	_9356SEL		BIT(4)
#define	_EECS			BIT(3)
#define	_EESK			BIT(2)
#define	_EEDI			BIT(1)
#define	_EEDO			BIT(0)

/*AFE_MISC*/
#define	AFE_MISC_USB_MBEN_SHT	7
#define	AFE_MISC_USB_MBEN	BIT(AFE_MISC_USB_MBEN_SHT)
#define	AFE_MISC_USB_BGEN_SHT	6
#define	AFE_MISC_USB_BGEN	BIT(AFE_MISC_USB_BGEN_SHT)
#define	AFE_MISC_LD12_VDAJ_SHT	4
#define	AFE_MISC_LD12_VDAJ_MSK	0X0030
#define	AFE_MISC_LD12_VDAJ	BIT(AFE_MISC_LD12_VDAJ_SHT)
#define	AFE_MISC_I32_EN_SHT	3
#define	AFE_MISC_I32_EN		BIT(AFE_MISC_I32_EN_SHT)
#define	AFE_MISC_E32_EN_SHT	2
#define	AFE_MISC_E32_EN		BIT(AFE_MISC_E32_EN_SHT)
#define	AFE_MISC_MBEN_SHT	1
#define	AFE_MISC_MBEN		BIT(AFE_MISC_MBEN_SHT)/* Enable AFE Macro
						       * Block's Mbias.
						       */
#define	AFE_MISC_BGEN_SHT	0
#define	AFE_MISC_BGEN		BIT(AFE_MISC_BGEN_SHT)/* Enable AFE Macro
						       * Block's Bandgap.
						       */


/*--------------------------------------------------------------------------*/
/*       SPS1_CTRL bits				(Offset 0x18-1E, 56bits)*/
/*--------------------------------------------------------------------------*/
#define	SPS1_SWEN		BIT(1)	/* Enable vsps18 SW Macro Block.*/
#define	SPS1_LDEN		BIT(0)	/* Enable VSPS12 LDO Macro block.*/


/*----------------------------------------------------------------------------*/
/*       LDOA15_CTRL bits		(Offset 0x20, 8bits)*/
/*----------------------------------------------------------------------------*/
#define	LDA15_EN		BIT(0)	/* Enable LDOA15 Macro Block*/


/*----------------------------------------------------------------------------*/
/*       8192S LDOV12D_CTRL bit		(Offset 0x21, 8bits)*/
/*----------------------------------------------------------------------------*/
#define	LDV12_EN		BIT(0)	/* Enable LDOVD12 Macro Block*/
#define	LDV12_SDBY		BIT(1)	/* LDOVD12 standby mode*/

/*CLK_PS_CTRL*/
#define	_CLK_GATE_EN		BIT(0)


/* EFUSE_CTRL*/
#define EF_FLAG			BIT(31)		/* Access Flag, Write:1;
						 *	        Read:0
						 */
#define EF_PGPD			0x70000000	/* E-fuse Program time*/
#define EF_RDT			0x0F000000	/* E-fuse read time: in the
						 * unit of cycle time
						 */
#define EF_PDN_EN		BIT(19)		/* EFuse Power down enable*/
#define ALD_EN			BIT(18)		/* Autoload Enable*/
#define EF_ADDR			0x0003FF00	/* Access Address*/
#define EF_DATA			0x000000FF	/* Access Data*/

/* EFUSE_TEST*/
#define LDOE25_EN		BIT(31)		/* Enable LDOE25 Macro Block*/

/* EFUSE_CLK_CTRL*/
#define EFUSE_CLK_EN		BIT(1)		/* E-Fuse Clock Enable*/
#define EFUSE_CLK_SEL		BIT(0)		/* E-Fuse Clock Select,
						 * 0:500K, 1:40M
						 */

#endif	/*__RTL8712_SYSCFG_BITDEF_H__*/

