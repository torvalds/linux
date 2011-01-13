/*
 * topic.h 1.8 1999/08/28 04:01:47
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 * topic.h $Release$ 1999/08/28 04:01:47
 */

#ifndef _LINUX_TOPIC_H
#define _LINUX_TOPIC_H

/* Register definitions for Toshiba ToPIC95/97/100 controllers */

#define TOPIC_SOCKET_CONTROL		0x0090	/* 32 bit */
#define  TOPIC_SCR_IRQSEL		0x00000001

#define TOPIC_SLOT_CONTROL		0x00a0	/* 8 bit */
#define  TOPIC_SLOT_SLOTON		0x80
#define  TOPIC_SLOT_SLOTEN		0x40
#define  TOPIC_SLOT_ID_LOCK		0x20
#define  TOPIC_SLOT_ID_WP		0x10
#define  TOPIC_SLOT_PORT_MASK		0x0c
#define  TOPIC_SLOT_PORT_SHIFT		2
#define  TOPIC_SLOT_OFS_MASK		0x03

#define TOPIC_CARD_CONTROL		0x00a1	/* 8 bit */
#define  TOPIC_CCR_INTB			0x20
#define  TOPIC_CCR_INTA			0x10
#define  TOPIC_CCR_CLOCK		0x0c
#define  TOPIC_CCR_PCICLK		0x0c
#define  TOPIC_CCR_PCICLK_2		0x08
#define  TOPIC_CCR_CCLK			0x04

#define TOPIC97_INT_CONTROL		0x00a1	/* 8 bit */
#define  TOPIC97_ICR_INTB		0x20
#define  TOPIC97_ICR_INTA		0x10
#define  TOPIC97_ICR_STSIRQNP		0x04
#define  TOPIC97_ICR_IRQNP		0x02
#define  TOPIC97_ICR_IRQSEL		0x01

#define TOPIC_CARD_DETECT		0x00a3	/* 8 bit */
#define  TOPIC_CDR_MODE_PC32		0x80
#define  TOPIC_CDR_VS1			0x04
#define  TOPIC_CDR_VS2			0x02
#define  TOPIC_CDR_SW_DETECT		0x01

#define TOPIC_REGISTER_CONTROL		0x00a4	/* 32 bit */
#define  TOPIC_RCR_RESUME_RESET		0x80000000
#define  TOPIC_RCR_REMOVE_RESET		0x40000000
#define  TOPIC97_RCR_CLKRUN_ENA		0x20000000
#define  TOPIC97_RCR_TESTMODE		0x10000000
#define  TOPIC97_RCR_IOPLUP		0x08000000
#define  TOPIC_RCR_BUFOFF_PWROFF	0x02000000
#define  TOPIC_RCR_BUFOFF_SIGOFF	0x01000000
#define  TOPIC97_RCR_CB_DEV_MASK	0x0000f800
#define  TOPIC97_RCR_CB_DEV_SHIFT	11
#define  TOPIC97_RCR_RI_DISABLE		0x00000004
#define  TOPIC97_RCR_CAUDIO_OFF		0x00000002
#define  TOPIC_RCR_CAUDIO_INVERT	0x00000001

#define TOPIC97_MISC1			0x00ad  /* 8bit */
#define  TOPIC97_MISC1_CLOCKRUN_ENABLE	0x80
#define  TOPIC97_MISC1_CLOCKRUN_MODE	0x40
#define  TOPIC97_MISC1_DETECT_REQ_ENA	0x10
#define  TOPIC97_MISC1_SCK_CLEAR_DIS	0x04
#define  TOPIC97_MISC1_R2_LOW_ENABLE	0x10

#define TOPIC97_MISC2			0x00ae  /* 8 bit */
#define  TOPIC97_MISC2_SPWRCLK_MASK	0x70
#define  TOPIC97_MISC2_SPWRMOD		0x08
#define  TOPIC97_MISC2_SPWR_ENABLE	0x04
#define  TOPIC97_MISC2_ZV_MODE		0x02
#define  TOPIC97_MISC2_ZV_ENABLE	0x01

#define TOPIC97_ZOOM_VIDEO_CONTROL	0x009c  /* 8 bit */
#define  TOPIC97_ZV_CONTROL_ENABLE	0x01

#define TOPIC97_AUDIO_VIDEO_SWITCH	0x003c  /* 8 bit */
#define  TOPIC97_AVS_AUDIO_CONTROL	0x02
#define  TOPIC97_AVS_VIDEO_CONTROL	0x01

#define TOPIC_EXCA_IF_CONTROL		0x3e	/* 8 bit */
#define TOPIC_EXCA_IFC_33V_ENA		0x01

static void topic97_zoom_video(struct pcmcia_socket *sock, int onoff)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	u8 reg_zv, reg;

	reg_zv = config_readb(socket, TOPIC97_ZOOM_VIDEO_CONTROL);
	if (onoff) {
		reg_zv |= TOPIC97_ZV_CONTROL_ENABLE;
		config_writeb(socket, TOPIC97_ZOOM_VIDEO_CONTROL, reg_zv);

		reg = config_readb(socket, TOPIC97_AUDIO_VIDEO_SWITCH);
		reg |= TOPIC97_AVS_AUDIO_CONTROL | TOPIC97_AVS_VIDEO_CONTROL;
		config_writeb(socket, TOPIC97_AUDIO_VIDEO_SWITCH, reg);
	} else {
		reg_zv &= ~TOPIC97_ZV_CONTROL_ENABLE;
		config_writeb(socket, TOPIC97_ZOOM_VIDEO_CONTROL, reg_zv);

		reg = config_readb(socket, TOPIC97_AUDIO_VIDEO_SWITCH);
		reg &= ~(TOPIC97_AVS_AUDIO_CONTROL | TOPIC97_AVS_VIDEO_CONTROL);
		config_writeb(socket, TOPIC97_AUDIO_VIDEO_SWITCH, reg);
	}
}

static int topic97_override(struct yenta_socket *socket)
{
	/* ToPIC97/100 support ZV */
	socket->socket.zoom_video = topic97_zoom_video;
	return 0;
}


static int topic95_override(struct yenta_socket *socket)
{
	u8 fctrl;

	/* enable 3.3V support for 16bit cards */
	fctrl = exca_readb(socket, TOPIC_EXCA_IF_CONTROL);
	exca_writeb(socket, TOPIC_EXCA_IF_CONTROL, fctrl | TOPIC_EXCA_IFC_33V_ENA);

	/* tell yenta to use exca registers to power 16bit cards */
	socket->flags |= YENTA_16BIT_POWER_EXCA | YENTA_16BIT_POWER_DF;

	return 0;
}

#endif /* _LINUX_TOPIC_H */
