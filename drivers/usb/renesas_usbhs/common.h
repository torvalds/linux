/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef RENESAS_USB_DRIVER_H
#define RENESAS_USB_DRIVER_H

#include <linux/platform_device.h>
#include <linux/usb/renesas_usbhs.h>

struct usbhs_priv;

#include "./mod.h"
#include "./pipe.h"

/*
 *
 *		register define
 *
 */
#define SYSCFG		0x0000
#define BUSWAIT		0x0002
#define DVSTCTR		0x0008
#define CFIFO		0x0014
#define CFIFOSEL	0x0020
#define CFIFOCTR	0x0022
#define INTENB0		0x0030
#define INTENB1		0x0032
#define BRDYENB		0x0036
#define NRDYENB		0x0038
#define BEMPENB		0x003A
#define INTSTS0		0x0040
#define INTSTS1		0x0042
#define BRDYSTS		0x0046
#define NRDYSTS		0x0048
#define BEMPSTS		0x004A
#define FRMNUM		0x004C
#define USBREQ		0x0054	/* USB request type register */
#define USBVAL		0x0056	/* USB request value register */
#define USBINDX		0x0058	/* USB request index register */
#define USBLENG		0x005A	/* USB request length register */
#define DCPCFG		0x005C
#define DCPMAXP		0x005E
#define DCPCTR		0x0060
#define PIPESEL		0x0064
#define PIPECFG		0x0068
#define PIPEBUF		0x006A
#define PIPEMAXP	0x006C
#define PIPEPERI	0x006E
#define PIPEnCTR	0x0070

/* SYSCFG */
#define SCKE	(1 << 10)	/* USB Module Clock Enable */
#define HSE	(1 << 7)	/* High-Speed Operation Enable */
#define DCFM	(1 << 6)	/* Controller Function Select */
#define DRPD	(1 << 5)	/* D+ Line/D- Line Resistance Control */
#define DPRPU	(1 << 4)	/* D+ Line Resistance Control */
#define USBE	(1 << 0)	/* USB Module Operation Enable */

/* DVSTCTR */
#define EXTLP	(1 << 10)	/* Controls the EXTLP pin output state */
#define PWEN	(1 << 9)	/* Controls the PWEN pin output state */
#define RHST	(0x7)		/* Reset Handshake */
#define  RHST_LOW_SPEED  1	/* Low-speed connection */
#define  RHST_FULL_SPEED 2	/* Full-speed connection */
#define  RHST_HIGH_SPEED 3	/* High-speed connection */

/* CFIFOSEL */
#define MBW_32	(0x2 << 10)	/* CFIFO Port Access Bit Width */

/* CFIFOCTR */
#define BVAL	(1 << 15)	/* Buffer Memory Enable Flag */
#define BCLR	(1 << 14)	/* CPU buffer clear */
#define FRDY	(1 << 13)	/* FIFO Port Ready */
#define DTLN_MASK (0x0FFF)	/* Receive Data Length */

/* INTENB0 */
#define VBSE	(1 << 15)	/* Enable IRQ VBUS_0 and VBUSIN_0 */
#define RSME	(1 << 14)	/* Enable IRQ Resume */
#define SOFE	(1 << 13)	/* Enable IRQ Frame Number Update */
#define DVSE	(1 << 12)	/* Enable IRQ Device State Transition */
#define CTRE	(1 << 11)	/* Enable IRQ Control Stage Transition */
#define BEMPE	(1 << 10)	/* Enable IRQ Buffer Empty */
#define NRDYE	(1 << 9)	/* Enable IRQ Buffer Not Ready Response */
#define BRDYE	(1 << 8)	/* Enable IRQ Buffer Ready */

/* INTENB1 */
#define BCHGE	(1 << 14)	/* USB Bus Change Interrupt Enable */
#define DTCHE	(1 << 12)	/* Disconnection Detect Interrupt Enable */
#define ATTCHE	(1 << 11)	/* Connection Detect Interrupt Enable */
#define EOFERRE	(1 << 6)	/* EOF Error Detect Interrupt Enable */
#define SIGNE	(1 << 5)	/* Setup Transaction Error Interrupt Enable */
#define SACKE	(1 << 4)	/* Setup Transaction ACK Interrupt Enable */

/* INTSTS0 */
#define VBINT	(1 << 15)	/* VBUS0_0 and VBUS1_0 Interrupt Status */
#define DVST	(1 << 12)	/* Device State Transition Interrupt Status */
#define CTRT	(1 << 11)	/* Control Stage Interrupt Status */
#define BEMP	(1 << 10)	/* Buffer Empty Interrupt Status */
#define BRDY	(1 << 8)	/* Buffer Ready Interrupt Status */
#define VBSTS	(1 << 7)	/* VBUS_0 and VBUSIN_0 Input Status */
#define VALID	(1 << 3)	/* USB Request Receive */

#define DVSQ_MASK		(0x3 << 4)	/* Device State */
#define  POWER_STATE		(0 << 4)
#define  DEFAULT_STATE		(1 << 4)
#define  ADDRESS_STATE		(2 << 4)
#define  CONFIGURATION_STATE	(3 << 4)

#define CTSQ_MASK		(0x7)	/* Control Transfer Stage */
#define  IDLE_SETUP_STAGE	0	/* Idle stage or setup stage */
#define  READ_DATA_STAGE	1	/* Control read data stage */
#define  READ_STATUS_STAGE	2	/* Control read status stage */
#define  WRITE_DATA_STAGE	3	/* Control write data stage */
#define  WRITE_STATUS_STAGE	4	/* Control write status stage */
#define  NODATA_STATUS_STAGE	5	/* Control write NoData status stage */
#define  SEQUENCE_ERROR		6	/* Control transfer sequence error */

/* PIPECFG */
/* DCPCFG */
#define TYPE_NONE	(0 << 14)	/* Transfer Type */
#define TYPE_BULK	(1 << 14)
#define TYPE_INT	(2 << 14)
#define TYPE_ISO	(3 << 14)
#define DBLB		(1 << 9)	/* Double Buffer Mode */
#define SHTNAK		(1 << 7)	/* Pipe Disable in Transfer End */
#define DIR_OUT		(1 << 4)	/* Transfer Direction */

/* PIPEMAXP */
/* DCPMAXP */
#define DEVSEL_MASK	(0xF << 12)	/* Device Select */
#define DCP_MAXP_MASK	(0x7F)
#define PIPE_MAXP_MASK	(0x7FF)

/* PIPEBUF */
#define BUFSIZE_SHIFT	10
#define BUFSIZE_MASK	(0x1F << BUFSIZE_SHIFT)
#define BUFNMB_MASK	(0xFF)

/* PIPEnCTR */
/* DCPCTR */
#define BSTS		(1 << 15)	/* Buffer Status */
#define CSSTS		(1 << 12)	/* CSSTS Status */
#define SQCLR		(1 << 8)	/* Toggle Bit Clear */
#define	ACLRM		(1 << 9)	/* Buffer Auto-Clear Mode */
#define PBUSY		(1 << 5)	/* Pipe Busy */
#define PID_MASK	(0x3)		/* Response PID */
#define  PID_NAK	0
#define  PID_BUF	1
#define  PID_STALL10	2
#define  PID_STALL11	3

#define CCPL		(1 << 2)	/* Control Transfer End Enable */

/* FRMNUM */
#define FRNM_MASK	(0x7FF)

/*
 *		struct
 */
struct usbhs_priv {

	void __iomem *base;
	unsigned int irq;

	struct renesas_usbhs_platform_callback	*pfunc;
	struct renesas_usbhs_driver_param	*dparam;

	struct delayed_work notify_hotplug_work;
	struct platform_device *pdev;

	spinlock_t		lock;

	u32 flags;

	/*
	 * module control
	 */
	struct usbhs_mod_info mod_info;

	/*
	 * pipe control
	 */
	struct usbhs_pipe_info pipe_info;
};

/*
 * common
 */
u16 usbhs_read(struct usbhs_priv *priv, u32 reg);
void usbhs_write(struct usbhs_priv *priv, u32 reg, u16 data);
void usbhs_bset(struct usbhs_priv *priv, u32 reg, u16 mask, u16 data);

int usbhsc_drvcllbck_notify_hotplug(struct platform_device *pdev);
/*
 * sysconfig
 */
void usbhs_sys_clock_ctrl(struct usbhs_priv *priv, int enable);
void usbhs_sys_hispeed_ctrl(struct usbhs_priv *priv, int enable);
void usbhs_sys_usb_ctrl(struct usbhs_priv *priv, int enable);
void usbhs_sys_host_ctrl(struct usbhs_priv *priv, int enable);
void usbhs_sys_function_ctrl(struct usbhs_priv *priv, int enable);

/*
 * frame
 */
int usbhs_frame_get_num(struct usbhs_priv *priv);

/*
 * data
 */
struct usbhs_priv *usbhs_pdev_to_priv(struct platform_device *pdev);
#define usbhs_get_dparam(priv, param)	(priv->dparam->param)
#define usbhs_priv_to_pdev(priv)	(priv->pdev)
#define usbhs_priv_to_dev(priv)		(&priv->pdev->dev)
#define usbhs_priv_to_lock(priv)	(&priv->lock)

#endif /* RENESAS_USB_DRIVER_H */
