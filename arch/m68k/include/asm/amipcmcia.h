/*
** asm-m68k/pcmcia.h -- Amiga Linux PCMCIA Definitions
**
** Copyright 1997 by Alain Malek
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created: 12/10/97 by Alain Malek
*/

#ifndef __AMIGA_PCMCIA_H__
#define __AMIGA_PCMCIA_H__

#include <asm/amigayle.h>

/* prototypes */

void pcmcia_reset(void);
int pcmcia_copy_tuple(unsigned char tuple_id, void *tuple, int max_len);
void pcmcia_program_voltage(int voltage);
void pcmcia_access_speed(int speed);
void pcmcia_write_enable(void);
void pcmcia_write_disable(void);

static inline u_char pcmcia_read_status(void)
{
	return (gayle.cardstatus & 0x7c);
}

static inline u_char pcmcia_get_intreq(void)
{
	return (gayle.intreq);
}

static inline void pcmcia_ack_int(u_char intreq)
{
	gayle.intreq = 0xf8;
}

static inline void pcmcia_enable_irq(void)
{
	gayle.inten |= GAYLE_IRQ_IRQ;
}

static inline void pcmcia_disable_irq(void)
{
	gayle.inten &= ~GAYLE_IRQ_IRQ;
}

#define PCMCIA_INSERTED	(gayle.cardstatus & GAYLE_CS_CCDET)

/* valid voltages for pcmcia_ProgramVoltage */

#define PCMCIA_0V	0
#define PCMCIA_5V	5
#define PCMCIA_12V	12

/* valid speeds for pcmcia_AccessSpeed */

#define PCMCIA_SPEED_100NS	100
#define PCMCIA_SPEED_150NS	150
#define PCMCIA_SPEED_250NS	250
#define PCMCIA_SPEED_720NS	720

/* PCMCIA Tuple codes */

#define CISTPL_NULL		0x00
#define CISTPL_DEVICE		0x01
#define CISTPL_LONGLINK_CB	0x02
#define CISTPL_CONFIG_CB	0x04
#define CISTPL_CFTABLE_ENTRY_CB	0x05
#define CISTPL_LONGLINK_MFC	0x06
#define CISTPL_BAR		0x07
#define CISTPL_CHECKSUM		0x10
#define CISTPL_LONGLINK_A	0x11
#define CISTPL_LONGLINK_C	0x12
#define CISTPL_LINKTARGET	0x13
#define CISTPL_NO_LINK		0x14
#define CISTPL_VERS_1		0x15
#define CISTPL_ALTSTR		0x16
#define CISTPL_DEVICE_A		0x17
#define CISTPL_JEDEC_C		0x18
#define CISTPL_JEDEC_A		0x19
#define CISTPL_CONFIG		0x1a
#define CISTPL_CFTABLE_ENTRY	0x1b
#define CISTPL_DEVICE_OC	0x1c
#define CISTPL_DEVICE_OA	0x1d
#define CISTPL_DEVICE_GEO	0x1e
#define CISTPL_DEVICE_GEO_A	0x1f
#define CISTPL_MANFID		0x20
#define CISTPL_FUNCID		0x21
#define CISTPL_FUNCE		0x22
#define CISTPL_SWIL		0x23
#define CISTPL_END		0xff

/* FUNCID */

#define CISTPL_FUNCID_MULTI	0x00
#define CISTPL_FUNCID_MEMORY	0x01
#define CISTPL_FUNCID_SERIAL	0x02
#define CISTPL_FUNCID_PARALLEL	0x03
#define CISTPL_FUNCID_FIXED	0x04
#define CISTPL_FUNCID_VIDEO	0x05
#define CISTPL_FUNCID_NETWORK	0x06
#define CISTPL_FUNCID_AIMS	0x07
#define CISTPL_FUNCID_SCSI	0x08

#endif
