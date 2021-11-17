/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _POWERPC_PMI_H
#define _POWERPC_PMI_H

/*
 * Definitions for talking with PMI device on PowerPC
 *
 * PMI (Platform Management Interrupt) is a way to communicate
 * with the BMC (Baseboard Management Controller) via interrupts.
 * Unlike IPMI it is bidirectional and has a low latency.
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 */

#ifdef __KERNEL__

#define PMI_TYPE_FREQ_CHANGE	0x01
#define PMI_TYPE_POWER_BUTTON	0x02
#define PMI_READ_TYPE		0
#define PMI_READ_DATA0		1
#define PMI_READ_DATA1		2
#define PMI_READ_DATA2		3
#define PMI_WRITE_TYPE		4
#define PMI_WRITE_DATA0		5
#define PMI_WRITE_DATA1		6
#define PMI_WRITE_DATA2		7

#define PMI_ACK			0x80

#define PMI_TIMEOUT		100

typedef struct {
	u8	type;
	u8	data0;
	u8	data1;
	u8	data2;
} pmi_message_t;

struct pmi_handler {
	struct list_head node;
	u8 type;
	void (*handle_pmi_message) (pmi_message_t);
};

int pmi_register_handler(struct pmi_handler *);
void pmi_unregister_handler(struct pmi_handler *);

int pmi_send_message(pmi_message_t);

#endif /* __KERNEL__ */
#endif /* _POWERPC_PMI_H */
