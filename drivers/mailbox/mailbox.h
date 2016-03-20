/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MAILBOX_H
#define __MAILBOX_H

#define TXDONE_BY_IRQ	BIT(0) /* controller has remote RTR irq */
#define TXDONE_BY_POLL	BIT(1) /* controller can read status of last TX */
#define TXDONE_BY_ACK	BIT(2) /* S/W ACK recevied by Client ticks the TX */

#endif /* __MAILBOX_H */
