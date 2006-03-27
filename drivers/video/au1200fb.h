/*
 * BRIEF MODULE DESCRIPTION
 *	Hardware definitions for the Au1200 LCD controller
 *
 * Copyright 2004 AMD
 * Author:	AMD
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _AU1200LCD_H
#define _AU1200LCD_H

/********************************************************************/
#define AU1200_LCD_ADDR		0xB5000000

#define uint8 unsigned char
#define uint32 unsigned int

struct au1200_lcd {
	volatile uint32	reserved0;
	volatile uint32	screen;
	volatile uint32	backcolor;
	volatile uint32	horztiming;
	volatile uint32	verttiming;
	volatile uint32	clkcontrol;
	volatile uint32	pwmdiv;
	volatile uint32	pwmhi;
	volatile uint32	reserved1;
	volatile uint32	winenable;
	volatile uint32	colorkey;
	volatile uint32	colorkeymsk;
	struct
	{
		volatile uint32	cursorctrl;
		volatile uint32	cursorpos;
		volatile uint32	cursorcolor0;
		volatile uint32	cursorcolor1;
		volatile uint32	cursorcolor2;
		uint32	cursorcolor3;
	} hwc;
	volatile uint32	intstatus;
	volatile uint32	intenable;
	volatile uint32	outmask;
	volatile uint32	fifoctrl;
	uint32	reserved2[(0x0100-0x0058)/4];
	struct
	{
		volatile uint32	winctrl0;
		volatile uint32	winctrl1;
		volatile uint32	winctrl2;
		volatile uint32	winbuf0;
		volatile uint32	winbuf1;
		volatile uint32	winbufctrl;
		uint32	winreserved0;
		uint32	winreserved1;
	} window[4];

	uint32	reserved3[(0x0400-0x0180)/4];

	volatile uint32	palette[(0x0800-0x0400)/4];

	volatile uint8	cursorpattern[256];
};

/* lcd_screen */
#define LCD_SCREEN_SEN		(1<<31)
#define LCD_SCREEN_SX		(0x07FF<<19)
#define LCD_SCREEN_SY		(0x07FF<< 8)
#define LCD_SCREEN_SWP		(1<<7)
#define LCD_SCREEN_SWD		(1<<6)
#define LCD_SCREEN_PT		(7<<0)
#define LCD_SCREEN_PT_TFT	(0<<0)
#define LCD_SCREEN_SX_N(WIDTH)	((WIDTH-1)<<19)
#define LCD_SCREEN_SY_N(HEIGHT)	((HEIGHT-1)<<8)
#define LCD_SCREEN_PT_CSTN	(1<<0)
#define LCD_SCREEN_PT_CDSTN	(2<<0)
#define LCD_SCREEN_PT_M8STN	(3<<0)
#define LCD_SCREEN_PT_M4STN	(4<<0)

/* lcd_backcolor */
#define LCD_BACKCOLOR_SBGR		(0xFF<<16)
#define LCD_BACKCOLOR_SBGG		(0xFF<<8)
#define LCD_BACKCOLOR_SBGB		(0xFF<<0)
#define LCD_BACKCOLOR_SBGR_N(N)	((N)<<16)
#define LCD_BACKCOLOR_SBGG_N(N)	((N)<<8)
#define LCD_BACKCOLOR_SBGB_N(N)	((N)<<0)

/* lcd_winenable */
#define LCD_WINENABLE_WEN3		(1<<3)
#define LCD_WINENABLE_WEN2		(1<<2)
#define LCD_WINENABLE_WEN1		(1<<1)
#define LCD_WINENABLE_WEN0		(1<<0)

/* lcd_colorkey */
#define LCD_COLORKEY_CKR		(0xFF<<16)
#define LCD_COLORKEY_CKG		(0xFF<<8)
#define LCD_COLORKEY_CKB		(0xFF<<0)
#define LCD_COLORKEY_CKR_N(N)	((N)<<16)
#define LCD_COLORKEY_CKG_N(N)	((N)<<8)
#define LCD_COLORKEY_CKB_N(N)	((N)<<0)

/* lcd_colorkeymsk */
#define LCD_COLORKEYMSK_CKMR		(0xFF<<16)
#define LCD_COLORKEYMSK_CKMG		(0xFF<<8)
#define LCD_COLORKEYMSK_CKMB		(0xFF<<0)
#define LCD_COLORKEYMSK_CKMR_N(N)	((N)<<16)
#define LCD_COLORKEYMSK_CKMG_N(N)	((N)<<8)
#define LCD_COLORKEYMSK_CKMB_N(N)	((N)<<0)

/* lcd windows control 0 */
#define LCD_WINCTRL0_OX		(0x07FF<<21)
#define LCD_WINCTRL0_OY		(0x07FF<<10)
#define LCD_WINCTRL0_A		(0x00FF<<2)
#define LCD_WINCTRL0_AEN	(1<<1)
#define LCD_WINCTRL0_OX_N(N) ((N)<<21)
#define LCD_WINCTRL0_OY_N(N) ((N)<<10)
#define LCD_WINCTRL0_A_N(N) ((N)<<2)

/* lcd windows control 1 */
#define LCD_WINCTRL1_PRI	(3<<30)
#define LCD_WINCTRL1_PIPE	(1<<29)
#define LCD_WINCTRL1_FRM	(0xF<<25)
#define LCD_WINCTRL1_CCO	(1<<24)
#define LCD_WINCTRL1_PO		(3<<22)
#define LCD_WINCTRL1_SZX	(0x07FF<<11)
#define LCD_WINCTRL1_SZY	(0x07FF<<0)
#define LCD_WINCTRL1_FRM_1BPP	(0<<25)
#define LCD_WINCTRL1_FRM_2BPP	(1<<25)
#define LCD_WINCTRL1_FRM_4BPP	(2<<25)
#define LCD_WINCTRL1_FRM_8BPP	(3<<25)
#define LCD_WINCTRL1_FRM_12BPP	(4<<25)
#define LCD_WINCTRL1_FRM_16BPP655	(5<<25)
#define LCD_WINCTRL1_FRM_16BPP565	(6<<25)
#define LCD_WINCTRL1_FRM_16BPP556	(7<<25)
#define LCD_WINCTRL1_FRM_16BPPI1555	(8<<25)
#define LCD_WINCTRL1_FRM_16BPPI5551	(9<<25)
#define LCD_WINCTRL1_FRM_16BPPA1555	(10<<25)
#define LCD_WINCTRL1_FRM_16BPPA5551	(11<<25)
#define LCD_WINCTRL1_FRM_24BPP		(12<<25)
#define LCD_WINCTRL1_FRM_32BPP		(13<<25)
#define LCD_WINCTRL1_PRI_N(N)	((N)<<30)
#define LCD_WINCTRL1_PO_00		(0<<22)
#define LCD_WINCTRL1_PO_01		(1<<22)
#define LCD_WINCTRL1_PO_10		(2<<22)
#define LCD_WINCTRL1_PO_11		(3<<22)
#define LCD_WINCTRL1_SZX_N(N)	((N-1)<<11)
#define LCD_WINCTRL1_SZY_N(N)	((N-1)<<0)

/* lcd windows control 2 */
#define LCD_WINCTRL2_CKMODE		(3<<24)
#define LCD_WINCTRL2_DBM		(1<<23)
#define LCD_WINCTRL2_RAM		(3<<21)
#define LCD_WINCTRL2_BX			(0x1FFF<<8)
#define LCD_WINCTRL2_SCX		(0xF<<4)
#define LCD_WINCTRL2_SCY		(0xF<<0)
#define LCD_WINCTRL2_CKMODE_00		(0<<24)
#define LCD_WINCTRL2_CKMODE_01		(1<<24)
#define LCD_WINCTRL2_CKMODE_10		(2<<24)
#define LCD_WINCTRL2_CKMODE_11		(3<<24)
#define LCD_WINCTRL2_RAM_NONE		(0<<21)
#define LCD_WINCTRL2_RAM_PALETTE	(1<<21)
#define LCD_WINCTRL2_RAM_GAMMA		(2<<21)
#define LCD_WINCTRL2_RAM_BUFFER		(3<<21)
#define LCD_WINCTRL2_BX_N(N)	((N)<<8)
#define LCD_WINCTRL2_SCX_1		(0<<4)
#define LCD_WINCTRL2_SCX_2		(1<<4)
#define LCD_WINCTRL2_SCX_4		(2<<4)
#define LCD_WINCTRL2_SCY_1		(0<<0)
#define LCD_WINCTRL2_SCY_2		(1<<0)
#define LCD_WINCTRL2_SCY_4		(2<<0)

/* lcd windows buffer control */
#define LCD_WINBUFCTRL_DB		(1<<1)
#define LCD_WINBUFCTRL_DBN		(1<<0)

/* lcd_intstatus, lcd_intenable */
#define LCD_INT_IFO				(0xF<<14)
#define LCD_INT_IFU				(0xF<<10)
#define LCD_INT_OFO				(1<<9)
#define LCD_INT_OFU				(1<<8)
#define LCD_INT_WAIT			(1<<3)
#define LCD_INT_SD				(1<<2)
#define LCD_INT_SA				(1<<1)
#define LCD_INT_SS				(1<<0)

/* lcd_horztiming */
#define LCD_HORZTIMING_HND2		(0x1FF<<18)
#define LCD_HORZTIMING_HND1		(0x1FF<<9)
#define LCD_HORZTIMING_HPW		(0x1FF<<0)
#define LCD_HORZTIMING_HND2_N(N)(((N)-1)<<18)
#define LCD_HORZTIMING_HND1_N(N)(((N)-1)<<9)
#define LCD_HORZTIMING_HPW_N(N)	(((N)-1)<<0)

/* lcd_verttiming */
#define LCD_VERTTIMING_VND2		(0x1FF<<18)
#define LCD_VERTTIMING_VND1		(0x1FF<<9)
#define LCD_VERTTIMING_VPW		(0x1FF<<0)
#define LCD_VERTTIMING_VND2_N(N)(((N)-1)<<18)
#define LCD_VERTTIMING_VND1_N(N)(((N)-1)<<9)
#define LCD_VERTTIMING_VPW_N(N)	(((N)-1)<<0)

/* lcd_clkcontrol */
#define LCD_CLKCONTROL_EXT		(1<<22)
#define LCD_CLKCONTROL_DELAY	(3<<20)
#define LCD_CLKCONTROL_CDD		(1<<19)
#define LCD_CLKCONTROL_IB		(1<<18)
#define LCD_CLKCONTROL_IC		(1<<17)
#define LCD_CLKCONTROL_IH		(1<<16)
#define LCD_CLKCONTROL_IV		(1<<15)
#define LCD_CLKCONTROL_BF		(0x1F<<10)
#define LCD_CLKCONTROL_PCD		(0x3FF<<0)
#define LCD_CLKCONTROL_BF_N(N)	(((N)-1)<<10)
#define LCD_CLKCONTROL_PCD_N(N)	((N)<<0)

/* lcd_pwmdiv */
#define LCD_PWMDIV_EN			(1<<31)
#define LCD_PWMDIV_PWMDIV		(0x1FFFF<<0)
#define LCD_PWMDIV_PWMDIV_N(N)	((N)<<0)

/* lcd_pwmhi */
#define LCD_PWMHI_PWMHI1		(0xFFFF<<16)
#define LCD_PWMHI_PWMHI0		(0xFFFF<<0)
#define LCD_PWMHI_PWMHI1_N(N)	((N)<<16)
#define LCD_PWMHI_PWMHI0_N(N)	((N)<<0)

/* lcd_hwccon */
#define LCD_HWCCON_EN			(1<<0)

/* lcd_cursorpos */
#define LCD_CURSORPOS_HWCXOFF		(0x1F<<27)
#define LCD_CURSORPOS_HWCXPOS		(0x07FF<<16)
#define LCD_CURSORPOS_HWCYOFF		(0x1F<<11)
#define LCD_CURSORPOS_HWCYPOS		(0x07FF<<0)
#define LCD_CURSORPOS_HWCXOFF_N(N)	((N)<<27)
#define LCD_CURSORPOS_HWCXPOS_N(N)	((N)<<16)
#define LCD_CURSORPOS_HWCYOFF_N(N)	((N)<<11)
#define LCD_CURSORPOS_HWCYPOS_N(N)	((N)<<0)

/* lcd_cursorcolor */
#define LCD_CURSORCOLOR_HWCA		(0xFF<<24)
#define LCD_CURSORCOLOR_HWCR		(0xFF<<16)
#define LCD_CURSORCOLOR_HWCG		(0xFF<<8)
#define LCD_CURSORCOLOR_HWCB		(0xFF<<0)
#define LCD_CURSORCOLOR_HWCA_N(N)	((N)<<24)
#define LCD_CURSORCOLOR_HWCR_N(N)	((N)<<16)
#define LCD_CURSORCOLOR_HWCG_N(N)	((N)<<8)
#define LCD_CURSORCOLOR_HWCB_N(N)	((N)<<0)

/* lcd_fifoctrl */
#define LCD_FIFOCTRL_F3IF		(1<<29)
#define LCD_FIFOCTRL_F3REQ		(0x1F<<24)
#define LCD_FIFOCTRL_F2IF		(1<<29)
#define LCD_FIFOCTRL_F2REQ		(0x1F<<16)
#define LCD_FIFOCTRL_F1IF		(1<<29)
#define LCD_FIFOCTRL_F1REQ		(0x1F<<8)
#define LCD_FIFOCTRL_F0IF		(1<<29)
#define LCD_FIFOCTRL_F0REQ		(0x1F<<0)
#define LCD_FIFOCTRL_F3REQ_N(N)	((N-1)<<24)
#define LCD_FIFOCTRL_F2REQ_N(N)	((N-1)<<16)
#define LCD_FIFOCTRL_F1REQ_N(N)	((N-1)<<8)
#define LCD_FIFOCTRL_F0REQ_N(N)	((N-1)<<0)

/* lcd_outmask */
#define LCD_OUTMASK_MASK		(0x00FFFFFF)

/********************************************************************/
#endif /* _AU1200LCD_H */
/*
 * BRIEF MODULE DESCRIPTION
 *	Hardware definitions for the Au1200 LCD controller
 *
 * Copyright 2004 AMD
 * Author:	AMD
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _AU1200LCD_H
#define _AU1200LCD_H

/********************************************************************/
#define AU1200_LCD_ADDR		0xB5000000

#define uint8 unsigned char
#define uint32 unsigned int

struct au1200_lcd {
	volatile uint32	reserved0;
	volatile uint32	screen;
	volatile uint32	backcolor;
	volatile uint32	horztiming;
	volatile uint32	verttiming;
	volatile uint32	clkcontrol;
	volatile uint32	pwmdiv;
	volatile uint32	pwmhi;
	volatile uint32	reserved1;
	volatile uint32	winenable;
	volatile uint32	colorkey;
	volatile uint32	colorkeymsk;
	struct
	{
		volatile uint32	cursorctrl;
		volatile uint32	cursorpos;
		volatile uint32	cursorcolor0;
		volatile uint32	cursorcolor1;
		volatile uint32	cursorcolor2;
		uint32	cursorcolor3;
	} hwc;
	volatile uint32	intstatus;
	volatile uint32	intenable;
	volatile uint32	outmask;
	volatile uint32	fifoctrl;
	uint32	reserved2[(0x0100-0x0058)/4];
	struct
	{
		volatile uint32	winctrl0;
		volatile uint32	winctrl1;
		volatile uint32	winctrl2;
		volatile uint32	winbuf0;
		volatile uint32	winbuf1;
		volatile uint32	winbufctrl;
		uint32	winreserved0;
		uint32	winreserved1;
	} window[4];

	uint32	reserved3[(0x0400-0x0180)/4];

	volatile uint32	palette[(0x0800-0x0400)/4];

	volatile uint8	cursorpattern[256];
};

/* lcd_screen */
#define LCD_SCREEN_SEN		(1<<31)
#define LCD_SCREEN_SX		(0x07FF<<19)
#define LCD_SCREEN_SY		(0x07FF<< 8)
#define LCD_SCREEN_SWP		(1<<7)
#define LCD_SCREEN_SWD		(1<<6)
#define LCD_SCREEN_PT		(7<<0)
#define LCD_SCREEN_PT_TFT	(0<<0)
#define LCD_SCREEN_SX_N(WIDTH)	((WIDTH-1)<<19)
#define LCD_SCREEN_SY_N(HEIGHT)	((HEIGHT-1)<<8)
#define LCD_SCREEN_PT_CSTN	(1<<0)
#define LCD_SCREEN_PT_CDSTN	(2<<0)
#define LCD_SCREEN_PT_M8STN	(3<<0)
#define LCD_SCREEN_PT_M4STN	(4<<0)

/* lcd_backcolor */
#define LCD_BACKCOLOR_SBGR		(0xFF<<16)
#define LCD_BACKCOLOR_SBGG		(0xFF<<8)
#define LCD_BACKCOLOR_SBGB		(0xFF<<0)
#define LCD_BACKCOLOR_SBGR_N(N)	((N)<<16)
#define LCD_BACKCOLOR_SBGG_N(N)	((N)<<8)
#define LCD_BACKCOLOR_SBGB_N(N)	((N)<<0)

/* lcd_winenable */
#define LCD_WINENABLE_WEN3		(1<<3)
#define LCD_WINENABLE_WEN2		(1<<2)
#define LCD_WINENABLE_WEN1		(1<<1)
#define LCD_WINENABLE_WEN0		(1<<0)

/* lcd_colorkey */
#define LCD_COLORKEY_CKR		(0xFF<<16)
#define LCD_COLORKEY_CKG		(0xFF<<8)
#define LCD_COLORKEY_CKB		(0xFF<<0)
#define LCD_COLORKEY_CKR_N(N)	((N)<<16)
#define LCD_COLORKEY_CKG_N(N)	((N)<<8)
#define LCD_COLORKEY_CKB_N(N)	((N)<<0)

/* lcd_colorkeymsk */
#define LCD_COLORKEYMSK_CKMR		(0xFF<<16)
#define LCD_COLORKEYMSK_CKMG		(0xFF<<8)
#define LCD_COLORKEYMSK_CKMB		(0xFF<<0)
#define LCD_COLORKEYMSK_CKMR_N(N)	((N)<<16)
#define LCD_COLORKEYMSK_CKMG_N(N)	((N)<<8)
#define LCD_COLORKEYMSK_CKMB_N(N)	((N)<<0)

/* lcd windows control 0 */
#define LCD_WINCTRL0_OX		(0x07FF<<21)
#define LCD_WINCTRL0_OY		(0x07FF<<10)
#define LCD_WINCTRL0_A		(0x00FF<<2)
#define LCD_WINCTRL0_AEN	(1<<1)
#define LCD_WINCTRL0_OX_N(N) ((N)<<21)
#define LCD_WINCTRL0_OY_N(N) ((N)<<10)
#define LCD_WINCTRL0_A_N(N) ((N)<<2)

/* lcd windows control 1 */
#define LCD_WINCTRL1_PRI	(3<<30)
#define LCD_WINCTRL1_PIPE	(1<<29)
#define LCD_WINCTRL1_FRM	(0xF<<25)
#define LCD_WINCTRL1_CCO	(1<<24)
#define LCD_WINCTRL1_PO		(3<<22)
#define LCD_WINCTRL1_SZX	(0x07FF<<11)
#define LCD_WINCTRL1_SZY	(0x07FF<<0)
#define LCD_WINCTRL1_FRM_1BPP	(0<<25)
#define LCD_WINCTRL1_FRM_2BPP	(1<<25)
#define LCD_WINCTRL1_FRM_4BPP	(2<<25)
#define LCD_WINCTRL1_FRM_8BPP	(3<<25)
#define LCD_WINCTRL1_FRM_12BPP	(4<<25)
#define LCD_WINCTRL1_FRM_16BPP655	(5<<25)
#define LCD_WINCTRL1_FRM_16BPP565	(6<<25)
#define LCD_WINCTRL1_FRM_16BPP556	(7<<25)
#define LCD_WINCTRL1_FRM_16BPPI1555	(8<<25)
#define LCD_WINCTRL1_FRM_16BPPI5551	(9<<25)
#define LCD_WINCTRL1_FRM_16BPPA1555	(10<<25)
#define LCD_WINCTRL1_FRM_16BPPA5551	(11<<25)
#define LCD_WINCTRL1_FRM_24BPP		(12<<25)
#define LCD_WINCTRL1_FRM_32BPP		(13<<25)
#define LCD_WINCTRL1_PRI_N(N)	((N)<<30)
#define LCD_WINCTRL1_PO_00		(0<<22)
#define LCD_WINCTRL1_PO_01		(1<<22)
#define LCD_WINCTRL1_PO_10		(2<<22)
#define LCD_WINCTRL1_PO_11		(3<<22)
#define LCD_WINCTRL1_SZX_N(N)	((N-1)<<11)
#define LCD_WINCTRL1_SZY_N(N)	((N-1)<<0)

/* lcd windows control 2 */
#define LCD_WINCTRL2_CKMODE		(3<<24)
#define LCD_WINCTRL2_DBM		(1<<23)
#define LCD_WINCTRL2_RAM		(3<<21)
#define LCD_WINCTRL2_BX			(0x1FFF<<8)
#define LCD_WINCTRL2_SCX		(0xF<<4)
#define LCD_WINCTRL2_SCY		(0xF<<0)
#define LCD_WINCTRL2_CKMODE_00		(0<<24)
#define LCD_WINCTRL2_CKMODE_01		(1<<24)
#define LCD_WINCTRL2_CKMODE_10		(2<<24)
#define LCD_WINCTRL2_CKMODE_11		(3<<24)
#define LCD_WINCTRL2_RAM_NONE		(0<<21)
#define LCD_WINCTRL2_RAM_PALETTE	(1<<21)
#define LCD_WINCTRL2_RAM_GAMMA		(2<<21)
#define LCD_WINCTRL2_RAM_BUFFER		(3<<21)
#define LCD_WINCTRL2_BX_N(N)	((N)<<8)
#define LCD_WINCTRL2_SCX_1		(0<<4)
#define LCD_WINCTRL2_SCX_2		(1<<4)
#define LCD_WINCTRL2_SCX_4		(2<<4)
#define LCD_WINCTRL2_SCY_1		(0<<0)
#define LCD_WINCTRL2_SCY_2		(1<<0)
#define LCD_WINCTRL2_SCY_4		(2<<0)

/* lcd windows buffer control */
#define LCD_WINBUFCTRL_DB		(1<<1)
#define LCD_WINBUFCTRL_DBN		(1<<0)

/* lcd_intstatus, lcd_intenable */
#define LCD_INT_IFO				(0xF<<14)
#define LCD_INT_IFU				(0xF<<10)
#define LCD_INT_OFO				(1<<9)
#define LCD_INT_OFU				(1<<8)
#define LCD_INT_WAIT			(1<<3)
#define LCD_INT_SD				(1<<2)
#define LCD_INT_SA				(1<<1)
#define LCD_INT_SS				(1<<0)

/* lcd_horztiming */
#define LCD_HORZTIMING_HND2		(0x1FF<<18)
#define LCD_HORZTIMING_HND1		(0x1FF<<9)
#define LCD_HORZTIMING_HPW		(0x1FF<<0)
#define LCD_HORZTIMING_HND2_N(N)(((N)-1)<<18)
#define LCD_HORZTIMING_HND1_N(N)(((N)-1)<<9)
#define LCD_HORZTIMING_HPW_N(N)	(((N)-1)<<0)

/* lcd_verttiming */
#define LCD_VERTTIMING_VND2		(0x1FF<<18)
#define LCD_VERTTIMING_VND1		(0x1FF<<9)
#define LCD_VERTTIMING_VPW		(0x1FF<<0)
#define LCD_VERTTIMING_VND2_N(N)(((N)-1)<<18)
#define LCD_VERTTIMING_VND1_N(N)(((N)-1)<<9)
#define LCD_VERTTIMING_VPW_N(N)	(((N)-1)<<0)

/* lcd_clkcontrol */
#define LCD_CLKCONTROL_EXT		(1<<22)
#define LCD_CLKCONTROL_DELAY	(3<<20)
#define LCD_CLKCONTROL_CDD		(1<<19)
#define LCD_CLKCONTROL_IB		(1<<18)
#define LCD_CLKCONTROL_IC		(1<<17)
#define LCD_CLKCONTROL_IH		(1<<16)
#define LCD_CLKCONTROL_IV		(1<<15)
#define LCD_CLKCONTROL_BF		(0x1F<<10)
#define LCD_CLKCONTROL_PCD		(0x3FF<<0)
#define LCD_CLKCONTROL_BF_N(N)	(((N)-1)<<10)
#define LCD_CLKCONTROL_PCD_N(N)	((N)<<0)

/* lcd_pwmdiv */
#define LCD_PWMDIV_EN			(1<<31)
#define LCD_PWMDIV_PWMDIV		(0x1FFFF<<0)
#define LCD_PWMDIV_PWMDIV_N(N)	((N)<<0)

/* lcd_pwmhi */
#define LCD_PWMHI_PWMHI1		(0xFFFF<<16)
#define LCD_PWMHI_PWMHI0		(0xFFFF<<0)
#define LCD_PWMHI_PWMHI1_N(N)	((N)<<16)
#define LCD_PWMHI_PWMHI0_N(N)	((N)<<0)

/* lcd_hwccon */
#define LCD_HWCCON_EN			(1<<0)

/* lcd_cursorpos */
#define LCD_CURSORPOS_HWCXOFF		(0x1F<<27)
#define LCD_CURSORPOS_HWCXPOS		(0x07FF<<16)
#define LCD_CURSORPOS_HWCYOFF		(0x1F<<11)
#define LCD_CURSORPOS_HWCYPOS		(0x07FF<<0)
#define LCD_CURSORPOS_HWCXOFF_N(N)	((N)<<27)
#define LCD_CURSORPOS_HWCXPOS_N(N)	((N)<<16)
#define LCD_CURSORPOS_HWCYOFF_N(N)	((N)<<11)
#define LCD_CURSORPOS_HWCYPOS_N(N)	((N)<<0)

/* lcd_cursorcolor */
#define LCD_CURSORCOLOR_HWCA		(0xFF<<24)
#define LCD_CURSORCOLOR_HWCR		(0xFF<<16)
#define LCD_CURSORCOLOR_HWCG		(0xFF<<8)
#define LCD_CURSORCOLOR_HWCB		(0xFF<<0)
#define LCD_CURSORCOLOR_HWCA_N(N)	((N)<<24)
#define LCD_CURSORCOLOR_HWCR_N(N)	((N)<<16)
#define LCD_CURSORCOLOR_HWCG_N(N)	((N)<<8)
#define LCD_CURSORCOLOR_HWCB_N(N)	((N)<<0)

/* lcd_fifoctrl */
#define LCD_FIFOCTRL_F3IF		(1<<29)
#define LCD_FIFOCTRL_F3REQ		(0x1F<<24)
#define LCD_FIFOCTRL_F2IF		(1<<29)
#define LCD_FIFOCTRL_F2REQ		(0x1F<<16)
#define LCD_FIFOCTRL_F1IF		(1<<29)
#define LCD_FIFOCTRL_F1REQ		(0x1F<<8)
#define LCD_FIFOCTRL_F0IF		(1<<29)
#define LCD_FIFOCTRL_F0REQ		(0x1F<<0)
#define LCD_FIFOCTRL_F3REQ_N(N)	((N-1)<<24)
#define LCD_FIFOCTRL_F2REQ_N(N)	((N-1)<<16)
#define LCD_FIFOCTRL_F1REQ_N(N)	((N-1)<<8)
#define LCD_FIFOCTRL_F0REQ_N(N)	((N-1)<<0)

/* lcd_outmask */
#define LCD_OUTMASK_MASK		(0x00FFFFFF)

/********************************************************************/
#endif /* _AU1200LCD_H */
