/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * PRUSS Remote Processor specific types
 *
 * Copyright (C) 2014-2020 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 */

#ifndef _PRU_RPROC_H_
#define _PRU_RPROC_H_

/**
 * struct pruss_int_map - PRU system events _to_ channel and host mapping
 * @event: number of the system event
 * @chnl: channel number assigned to a given @event
 * @host: host number assigned to a given @chnl
 *
 * PRU system events are mapped to channels, and these channels are mapped
 * to host interrupts. Events can be mapped to channels in a one-to-one or
 * many-to-one ratio (multiple events per channel), and channels can be
 * mapped to host interrupts in a one-to-one or many-to-one ratio (multiple
 * channels per interrupt).
 */
struct pruss_int_map {
	u8 event;
	u8 chnl;
	u8 host;
};

/**
 * struct pru_irq_rsc - PRU firmware section header for IRQ data
 * @type: resource type
 * @num_evts: number of described events
 * @pru_intc_map: PRU interrupt routing description
 *
 * The PRU firmware blob can contain optional .pru_irq_map ELF section, which
 * provides the PRUSS interrupt mapping description. The pru_irq_rsc struct
 * describes resource entry format.
 */
struct pru_irq_rsc {
	u8 type;
	u8 num_evts;
	struct pruss_int_map pru_intc_map[];
} __packed;

#endif	/* _PRU_RPROC_H_ */
