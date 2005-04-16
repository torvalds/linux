/*
 * FILE NAME
 *	drivers/pcmcia/vrc4173_cardu.h
 *
 * BRIEF MODULE DESCRIPTION
 *	Include file for NEC VRC4173 CARDU.
 *
 * Copyright 2002 Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _VRC4173_CARDU_H
#define _VRC4173_CARDU_H

#include <linux/pci.h>

#include <pcmcia/ss.h>

#define CARDU_MAX_SOCKETS	2
#define CARDU1			0
#define CARDU2			1

/*
 * PCI Configuration Registers
 */
#define BRGCNT			0x3e
 #define POST_WR_EN		0x0400
 #define MEM1_PREF_EN		0x0200
 #define MEM0_PREF_EN		0x0100
 #define IREQ_INT		0x0080
 #define CARD_RST		0x0040
 #define MABORT_MODE		0x0020
 #define VGA_EN			0x0008
 #define ISA_EN			0x0004
 #define SERR_EN		0x0002
 #define PERR_EN		0x0001

#define SYSCNT			0x80
 #define BAD_VCC_REQ_DISB	0x00200000
 #define PCPCI_EN		0x00080000
 #define CH_ASSIGN_MASK		0x00070000
 #define CH_ASSIGN_NODMA	0x00040000
 #define SUB_ID_WR_EN		0x00000008
 #define ASYN_INT_MODE		0x00000004
 #define PCI_CLK_RIN		0x00000002

#define DEVCNT			0x91
 #define ZOOM_VIDEO_EN		0x40
 #define SR_PCI_INT_SEL_MASK	0x18
 #define SR_PCI_INT_SEL_NONE	0x00
 #define PCI_INT_MODE		0x04
 #define IRQ_MODE		0x02
 #define IFG			0x01

#define CHIPCNT			0x9c
 #define S_PREF_DISB		0x10

#define SERRDIS			0x9f
 #define SERR_DIS_MAB		0x10
 #define SERR_DIS_TAB		0x08
 #define SERR_DIS_DT_PERR	0x04

/*
 * ExCA Registers
 */
#define EXCA_REGS_BASE		0x800
#define EXCA_REGS_SIZE		0x800

#define ID_REV			0x000
 #define IF_TYPE_16BIT		0x80

#define IF_STATUS		0x001
 #define CARD_PWR		0x40
 #define READY			0x20
 #define CARD_WP		0x10
 #define CARD_DETECT2		0x08
 #define CARD_DETECT1		0x04
 #define BV_DETECT_MASK		0x03
 #define BV_DETECT_GOOD		0x03	/* Memory card */
 #define BV_DETECT_WARN		0x02
 #define BV_DETECT_BAD1		0x01
 #define BV_DETECT_BAD0		0x00
 #define STSCHG			0x02	/* I/O card */
 #define SPKR			0x01

#define PWR_CNT			0x002
 #define CARD_OUT_EN		0x80
 #define VCC_MASK		0x18
 #define VCC_3V			0x18
 #define VCC_5V			0x10
 #define VCC_0V			0x00
 #define VPP_MASK		0x03
 #define VPP_12V		0x02
 #define VPP_VCC		0x01
 #define VPP_0V			0x00

#define INT_GEN_CNT		0x003
 #define CARD_REST0		0x40
 #define CARD_TYPE_MASK		0x20
 #define CARD_TYPE_IO		0x20
 #define CARD_TYPE_MEM		0x00

#define CARD_SC			0x004
 #define CARD_DT_CHG		0x08
 #define RDY_CHG		0x04
 #define BAT_WAR_CHG		0x02
 #define BAT_DEAD_ST_CHG	0x01

#define CARD_SCI		0x005
 #define CARD_DT_EN		0x08
 #define RDY_EN			0x04
 #define BAT_WAR_EN		0x02
 #define BAT_DEAD_EN		0x01

#define ADR_WIN_EN		0x006
 #define IO_WIN_EN(x)		(0x40 << (x))
 #define MEM_WIN_EN(x)		(0x01 << (x))

#define IO_WIN_CNT		0x007
 #define IO_WIN_CNT_MASK(x)	(0x03 << ((x) << 2))
 #define IO_WIN_DATA_AUTOSZ(x)	(0x02 << ((x) << 2))
 #define IO_WIN_DATA_16BIT(x)	(0x01 << ((x) << 2))

#define IO_WIN_SA(x)		(0x008 + ((x) << 2))
#define IO_WIN_EA(x)		(0x00a + ((x) << 2))

#define MEM_WIN_SA(x)		(0x010 + ((x) << 3))
 #define MEM_WIN_DSIZE		0x8000

#define MEM_WIN_EA(x)		(0x012 + ((x) << 3))

#define MEM_WIN_OA(x)		(0x014 + ((x) << 3))
 #define MEM_WIN_WP		0x8000
 #define MEM_WIN_REGSET		0x4000

#define GEN_CNT			0x016
 #define VS2_STATUS		0x80
 #define VS1_STATUS		0x40
 #define EXCA_REG_RST_EN	0x02

#define GLO_CNT			0x01e
 #define FUN_INT_LEV		0x08
 #define INT_WB_CLR		0x04
 #define CSC_INT_LEV		0x02

#define IO_WIN_OAL(x)		(0x036 + ((x) << 1))
#define IO_WIN_OAH(x)		(0x037 + ((x) << 1))

#define MEM_WIN_SAU(x)		(0x040 + (x))

#define IO_SETUP_TIM		0x080
#define IO_CMD_TIM		0x081
#define IO_HOLD_TIM		0x082
#define MEM_SETUP_TIM(x)	(0x084 + ((x) << 2))
#define MEM_CMD_TIM(x)		(0x085 + ((x) << 2))
#define MEM_HOLD_TIM(x)		(0x086 + ((x) << 2))
 #define TIM_CLOCKS(x)		((x) - 1)

#define MEM_TIM_SEL1		0x08c
#define MEM_TIM_SEL2		0x08d
 #define MEM_WIN_TIMSEL1(x)	(0x03 << (((x) & 3) << 1))

#define MEM_WIN_PWEN		0x091
 #define POSTWEN		0x01

/*
 * CardBus Socket Registers
 */
#define CARDBUS_SOCKET_REGS_BASE	0x000
#define CARDBUS_SOCKET_REGS_SIZE	0x800

#define SKT_EV			0x000
 #define POW_CYC_EV		0x00000008
 #define CCD2_EV		0x00000004
 #define CCD1_EV		0x00000002
 #define CSTSCHG_EV		0x00000001

#define SKT_MASK		0x004
 #define POW_CYC_MASK		0x00000008
 #define CCD_MASK		0x00000006
 #define CSC_MASK		0x00000001

#define SKT_PRE_STATE		0x008
#define SKT_FORCE_EV		0x00c
 #define VOL_3V_SKT		0x20000000
 #define VOL_5V_SKT		0x10000000
 #define CVS_TEST		0x00004000
 #define VOL_YV_CARD_DT		0x00002000
 #define VOL_XV_CARD_DT		0x00001000
 #define VOL_3V_CARD_DT		0x00000800
 #define VOL_5V_CARD_DT		0x00000400
 #define BAD_VCC_REQ		0x00000200
 #define DATA_LOST		0x00000100
 #define NOT_A_CARD		0x00000080
 #define CREADY			0x00000040
 #define CB_CARD_DT		0x00000020
 #define R2_CARD_DT		0x00000010
 #define POW_UP			0x00000008
 #define CCD20			0x00000004
 #define CCD10			0x00000002
 #define CSTSCHG		0x00000001

#define SKT_CNT			0x010
 #define STP_CLK_EN		0x00000080
 #define VCC_CNT_MASK		0x00000070
 #define VCC_CNT_3V		0x00000030
 #define VCC_CNT_5V		0x00000020
 #define VCC_CNT_0V		0x00000000
 #define VPP_CNT_MASK		0x00000007
 #define VPP_CNT_3V		0x00000003
 #define VPP_CNT_5V		0x00000002
 #define VPP_CNT_12V		0x00000001
 #define VPP_CNT_0V		0x00000000

typedef struct vrc4173_socket {
	int noprobe;
	struct pci_dev *dev;
	void *base;
	void (*handler)(void *, unsigned int);
	void *info;
	socket_cap_t cap;
	spinlock_t event_lock;
	uint16_t events;
	struct socket_info_t *pcmcia_socket;
	struct work_struct tq_work;
	char name[20];
} vrc4173_socket_t;

#endif /* _VRC4173_CARDU_H */
