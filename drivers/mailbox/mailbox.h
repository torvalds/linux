/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MAILBOX_H
#define __MAILBOX_H

#define TXDONE_BY_IRQ	BIT(0) /* controller has remote RTR irq */
#define TXDONE_BY_POLL	BIT(1) /* controller can read status of last TX */
#define TXDONE_BY_ACK	BIT(2) /* S/W ACK recevied by Client ticks the TX */

#endif /* __MAILBOX_H */
