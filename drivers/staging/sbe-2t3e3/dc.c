/*
 * SBE 2T3E3 synchronous serial card driver for Linux
 *
 * Copyright (C) 2009-2010 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This code is based on a driver written by SBE Inc.
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/io.h>
#include "2t3e3.h"
#include "ctrl.h"

void dc_init(struct channel *sc)
{
	u32 val;

	dc_stop(sc);
	/*dc_reset(sc);*/ /* do not want to reset here */

	/*
	 * BUS_MODE (CSR0)
	 */
	val = SBE_2T3E3_21143_VAL_READ_LINE_ENABLE |
		SBE_2T3E3_21143_VAL_READ_MULTIPLE_ENABLE |
		SBE_2T3E3_21143_VAL_TRANSMIT_AUTOMATIC_POLLING_200us |
		SBE_2T3E3_21143_VAL_BUS_ARBITRATION_RR;

	if (sc->h.command & 16)
		val |= SBE_2T3E3_21143_VAL_WRITE_AND_INVALIDATE_ENABLE;

	switch (sc->h.cache_size) {
	case 32:
		val |= SBE_2T3E3_21143_VAL_CACHE_ALIGNMENT_32;
		break;
	case 16:
		val |= SBE_2T3E3_21143_VAL_CACHE_ALIGNMENT_16;
		break;
	case 8:
		val |= SBE_2T3E3_21143_VAL_CACHE_ALIGNMENT_8;
		break;
	default:
		break;
	}

	dc_write(sc->addr, SBE_2T3E3_21143_REG_BUS_MODE, val);

	/* OPERATION_MODE (CSR6) */
	val = SBE_2T3E3_21143_VAL_RECEIVE_ALL |
		SBE_2T3E3_21143_VAL_MUST_BE_ONE |
		SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_1 |
		SBE_2T3E3_21143_VAL_LOOPBACK_OFF |
		SBE_2T3E3_21143_VAL_PASS_ALL_MULTICAST |
		SBE_2T3E3_21143_VAL_PROMISCUOUS_MODE |
		SBE_2T3E3_21143_VAL_PASS_BAD_FRAMES;
	dc_write(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE, val);
	if (sc->p.loopback == SBE_2T3E3_LOOPBACK_ETHERNET)
		sc->p.loopback = SBE_2T3E3_LOOPBACK_NONE;

	/*
	 * GENERAL_PURPOSE_TIMER_AND_INTERRUPT_MITIGATION_CONTROL (CSR11)
	 */
	val = SBE_2T3E3_21143_VAL_CYCLE_SIZE |
		SBE_2T3E3_21143_VAL_TRANSMIT_TIMER |
		SBE_2T3E3_21143_VAL_NUMBER_OF_TRANSMIT_PACKETS |
		SBE_2T3E3_21143_VAL_RECEIVE_TIMER |
		SBE_2T3E3_21143_VAL_NUMBER_OF_RECEIVE_PACKETS;
	dc_write(sc->addr, SBE_2T3E3_21143_REG_GENERAL_PURPOSE_TIMER_AND_INTERRUPT_MITIGATION_CONTROL, val);

	/* prepare descriptors and data for receive and transmit processes */
	if (dc_init_descriptor_list(sc) != 0)
		return;

	/* clear ethernet interrupts status */
	dc_write(sc->addr, SBE_2T3E3_21143_REG_STATUS, 0xFFFFFFFF);

	/* SIA mode registers */
	dc_set_output_port(sc);
}

void dc_start(struct channel *sc)
{
	u32 val;

	if (!(sc->r.flags & SBE_2T3E3_FLAG_NETWORK_UP))
		return;

	dc_init(sc);

	/* get actual LOS and OOF status */
	switch (sc->p.frame_type) {
	case SBE_2T3E3_FRAME_TYPE_E3_G751:
	case SBE_2T3E3_FRAME_TYPE_E3_G832:
		val = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_E3_RX_CONFIGURATION_STATUS_2);
		dev_dbg(&sc->pdev->dev, "Start Framer Rx Status = %02X\n", val);
		sc->s.OOF = val & SBE_2T3E3_FRAMER_VAL_E3_RX_OOF ? 1 : 0;
		break;
	case SBE_2T3E3_FRAME_TYPE_T3_CBIT:
	case SBE_2T3E3_FRAME_TYPE_T3_M13:
		val = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_CONFIGURATION_STATUS);
		dev_dbg(&sc->pdev->dev, "Start Framer Rx Status = %02X\n", val);
		sc->s.OOF = val & SBE_2T3E3_FRAMER_VAL_T3_RX_OOF ? 1 : 0;
		break;
	default:
		break;
	}
	cpld_LOS_update(sc);

	/* start receive and transmit processes */
	dc_transmitter_onoff(sc, SBE_2T3E3_ON);
	dc_receiver_onoff(sc, SBE_2T3E3_ON);

	/* start interrupts */
	dc_start_intr(sc);
}

#define MAX_INT_WAIT_CNT	12000
void dc_stop(struct channel *sc)
{
	int wcnt;

	/* stop receive and transmit processes */
	dc_receiver_onoff(sc, SBE_2T3E3_OFF);
	dc_transmitter_onoff(sc, SBE_2T3E3_OFF);

	/* turn off ethernet interrupts */
	dc_stop_intr(sc);

	/* wait to ensure the interrupts have been completed */
	for (wcnt = 0; wcnt < MAX_INT_WAIT_CNT; wcnt++) {
		udelay(5);
		if (!sc->interrupt_active)
			break;
	}
	if (wcnt >= MAX_INT_WAIT_CNT)
		dev_warn(&sc->pdev->dev, "SBE 2T3E3: Interrupt active too long\n");

	/* clear all receive/transmit data */
	dc_drop_descriptor_list(sc);
}

void dc_start_intr(struct channel *sc)
{
	if (sc->p.loopback == SBE_2T3E3_LOOPBACK_NONE && sc->s.OOF)
		return;

	if (sc->p.receiver_on || sc->p.transmitter_on) {
		if (!sc->ether.interrupt_enable_mask)
			dc_write(sc->addr, SBE_2T3E3_21143_REG_STATUS, 0xFFFFFFFF);

		sc->ether.interrupt_enable_mask =
			SBE_2T3E3_21143_VAL_NORMAL_INTERRUPT_SUMMARY_ENABLE |
			SBE_2T3E3_21143_VAL_ABNORMAL_INTERRUPT_SUMMARY_ENABLE |
			SBE_2T3E3_21143_VAL_RECEIVE_STOPPED_ENABLE |
			SBE_2T3E3_21143_VAL_RECEIVE_BUFFER_UNAVAILABLE_ENABLE |
			SBE_2T3E3_21143_VAL_RECEIVE_INTERRUPT_ENABLE |
			SBE_2T3E3_21143_VAL_TRANSMIT_UNDERFLOW_INTERRUPT_ENABLE |
			SBE_2T3E3_21143_VAL_TRANSMIT_BUFFER_UNAVAILABLE_ENABLE |
			SBE_2T3E3_21143_VAL_TRANSMIT_STOPPED_ENABLE |
			SBE_2T3E3_21143_VAL_TRANSMIT_INTERRUPT_ENABLE;

		dc_write(sc->addr, SBE_2T3E3_21143_REG_INTERRUPT_ENABLE,
			 sc->ether.interrupt_enable_mask);
	}
}

void dc_stop_intr(struct channel *sc)
{
	sc->ether.interrupt_enable_mask = 0;
	dc_write(sc->addr, SBE_2T3E3_21143_REG_INTERRUPT_ENABLE, 0);
}

void dc_reset(struct channel *sc)
{
	/* turn off ethernet interrupts */
	dc_write(sc->addr, SBE_2T3E3_21143_REG_INTERRUPT_ENABLE, 0);
	dc_write(sc->addr, SBE_2T3E3_21143_REG_STATUS, 0xFFFFFFFF);

	/* software reset */
	dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_BUS_MODE,
		   SBE_2T3E3_21143_VAL_SOFTWARE_RESET);
	udelay(4); /* 50 PCI cycles < 2us */

	/* clear hardware configuration */
	dc_write(sc->addr, SBE_2T3E3_21143_REG_BUS_MODE, 0);

	/* clear software configuration */
	dc_write(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE, 0);

	/* turn off SIA reset */
	dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_SIA_CONNECTIVITY,
		   SBE_2T3E3_21143_VAL_SIA_RESET);
	dc_write(sc->addr, SBE_2T3E3_21143_REG_SIA_TRANSMIT_AND_RECEIVE, 0);
	dc_write(sc->addr, SBE_2T3E3_21143_REG_SIA_AND_GENERAL_PURPOSE_PORT, 0);
}


void dc_receiver_onoff(struct channel *sc, u32 mode)
{
	u32 i, state = 0;

	if (sc->p.receiver_on == mode)
		return;

	switch (mode) {
	case SBE_2T3E3_OFF:
		if (dc_read(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE) &
		    SBE_2T3E3_21143_VAL_RECEIVE_START) {
			dc_clear_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
				      SBE_2T3E3_21143_VAL_RECEIVE_START);

			for (i = 0; i < 16; i++) {
				state = dc_read(sc->addr, SBE_2T3E3_21143_REG_STATUS) &
					SBE_2T3E3_21143_VAL_RECEIVE_PROCESS_STATE;
				if (state == SBE_2T3E3_21143_VAL_RX_STOPPED)
					break;
				udelay(5);
			}
			if (state != SBE_2T3E3_21143_VAL_RX_STOPPED)
				dev_warn(&sc->pdev->dev, "SBE 2T3E3: Rx failed to stop\n");
			else
				dev_info(&sc->pdev->dev, "SBE 2T3E3: Rx off\n");
		}
		break;
	case SBE_2T3E3_ON:
		dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			   SBE_2T3E3_21143_VAL_RECEIVE_START);
		udelay(100);
		dc_write(sc->addr, SBE_2T3E3_21143_REG_RECEIVE_POLL_DEMAND, 0xFFFFFFFF);
		break;
	default:
		return;
	}

	sc->p.receiver_on = mode;
}

void dc_transmitter_onoff(struct channel *sc, u32 mode)
{
	u32 i, state = 0;

	if (sc->p.transmitter_on == mode)
		return;

	switch (mode) {
	case SBE_2T3E3_OFF:
		if (dc_read(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE) &
		    SBE_2T3E3_21143_VAL_TRANSMISSION_START) {
			dc_clear_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
				      SBE_2T3E3_21143_VAL_TRANSMISSION_START);

			for (i = 0; i < 16; i++) {
				state = dc_read(sc->addr, SBE_2T3E3_21143_REG_STATUS) &
					SBE_2T3E3_21143_VAL_TRANSMISSION_PROCESS_STATE;
				if (state == SBE_2T3E3_21143_VAL_TX_STOPPED)
					break;
				udelay(5);
			}
			if (state != SBE_2T3E3_21143_VAL_TX_STOPPED)
				dev_warn(&sc->pdev->dev, "SBE 2T3E3: Tx failed to stop\n");
		}
		break;
	case SBE_2T3E3_ON:
		dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			   SBE_2T3E3_21143_VAL_TRANSMISSION_START);
		udelay(100);
		dc_write(sc->addr, SBE_2T3E3_21143_REG_TRANSMIT_POLL_DEMAND, 0xFFFFFFFF);
		break;
	default:
		return;
	}

	sc->p.transmitter_on = mode;
}



void dc_set_loopback(struct channel *sc, u32 mode)
{
	u32 val;

	switch (mode) {
	case SBE_2T3E3_21143_VAL_LOOPBACK_OFF:
	case SBE_2T3E3_21143_VAL_LOOPBACK_INTERNAL:
		break;
	default:
		return;
	}

	/* select loopback mode */
	val = dc_read(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE) &
		~SBE_2T3E3_21143_VAL_OPERATING_MODE;
	val |= mode;
	dc_write(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE, val);

	if (mode == SBE_2T3E3_21143_VAL_LOOPBACK_OFF)
		dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			   SBE_2T3E3_21143_VAL_FULL_DUPLEX_MODE);
	else
		dc_clear_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			      SBE_2T3E3_21143_VAL_FULL_DUPLEX_MODE);
}

u32 dc_init_descriptor_list(struct channel *sc)
{
	u32 i, j;
	struct sk_buff *m;

	if (sc->ether.rx_ring == NULL)
		sc->ether.rx_ring = kzalloc(SBE_2T3E3_RX_DESC_RING_SIZE *
					    sizeof(t3e3_rx_desc_t), GFP_KERNEL);
	if (sc->ether.rx_ring == NULL) {
		dev_err(&sc->pdev->dev, "SBE 2T3E3: no buffer space for RX ring\n");
		return ENOMEM;
	}

	if (sc->ether.tx_ring == NULL)
		sc->ether.tx_ring = kzalloc(SBE_2T3E3_TX_DESC_RING_SIZE *
					    sizeof(t3e3_tx_desc_t), GFP_KERNEL);
	if (sc->ether.tx_ring == NULL) {
		kfree(sc->ether.rx_ring);
		sc->ether.rx_ring = NULL;
		dev_err(&sc->pdev->dev, "SBE 2T3E3: no buffer space for RX ring\n");
		return ENOMEM;
	}


	/*
	 * Receive ring
	 */
	for (i = 0; i < SBE_2T3E3_RX_DESC_RING_SIZE; i++) {
		sc->ether.rx_ring[i].rdes0 = SBE_2T3E3_RX_DESC_21143_OWN;
		sc->ether.rx_ring[i].rdes1 =
			SBE_2T3E3_RX_DESC_SECOND_ADDRESS_CHAINED | SBE_2T3E3_MTU;

		if (sc->ether.rx_data[i] == NULL) {
			if (!(m = dev_alloc_skb(MCLBYTES))) {
				for (j = 0; j < i; j++) {
					dev_kfree_skb_any(sc->ether.rx_data[j]);
					sc->ether.rx_data[j] = NULL;
				}
				kfree(sc->ether.rx_ring);
				sc->ether.rx_ring = NULL;
				kfree(sc->ether.tx_ring);
				sc->ether.tx_ring = NULL;
				dev_err(&sc->pdev->dev, "SBE 2T3E3: token_alloc err:"
					" no buffer space for RX ring\n");
				return ENOBUFS;
			}
			sc->ether.rx_data[i] = m;
		}
		sc->ether.rx_ring[i].rdes2 = virt_to_phys(sc->ether.rx_data[i]->data);

		sc->ether.rx_ring[i].rdes3 = virt_to_phys(
			&sc->ether.rx_ring[(i + 1) % SBE_2T3E3_RX_DESC_RING_SIZE]);
	}
	sc->ether.rx_ring[SBE_2T3E3_RX_DESC_RING_SIZE - 1].rdes1 |=
		SBE_2T3E3_RX_DESC_END_OF_RING;
	sc->ether.rx_ring_current_read = 0;

	dc_write(sc->addr, SBE_2T3E3_21143_REG_RECEIVE_LIST_BASE_ADDRESS,
		 virt_to_phys(&sc->ether.rx_ring[0]));

	/*
	 * Transmit ring
	 */
	for (i = 0; i < SBE_2T3E3_TX_DESC_RING_SIZE; i++) {
		sc->ether.tx_ring[i].tdes0 = 0;
		sc->ether.tx_ring[i].tdes1 = SBE_2T3E3_TX_DESC_SECOND_ADDRESS_CHAINED |
			SBE_2T3E3_TX_DESC_DISABLE_PADDING;

		sc->ether.tx_ring[i].tdes2 = 0;
		sc->ether.tx_data[i] = NULL;

		sc->ether.tx_ring[i].tdes3 = virt_to_phys(
			&sc->ether.tx_ring[(i + 1) % SBE_2T3E3_TX_DESC_RING_SIZE]);
	}
	sc->ether.tx_ring[SBE_2T3E3_TX_DESC_RING_SIZE - 1].tdes1 |=
		SBE_2T3E3_TX_DESC_END_OF_RING;

	dc_write(sc->addr, SBE_2T3E3_21143_REG_TRANSMIT_LIST_BASE_ADDRESS,
		 virt_to_phys(&sc->ether.tx_ring[0]));
	sc->ether.tx_ring_current_read = 0;
	sc->ether.tx_ring_current_write = 0;
	sc->ether.tx_free_cnt = SBE_2T3E3_TX_DESC_RING_SIZE;
	spin_lock_init(&sc->ether.tx_lock);

	return 0;
}

void dc_clear_descriptor_list(struct channel *sc)
{
	u32 i;

	/* clear CSR3 and CSR4 */
	dc_write(sc->addr, SBE_2T3E3_21143_REG_RECEIVE_LIST_BASE_ADDRESS, 0);
	dc_write(sc->addr, SBE_2T3E3_21143_REG_TRANSMIT_LIST_BASE_ADDRESS, 0);

	/* free all data buffers on TX ring */
	for (i = 0; i < SBE_2T3E3_TX_DESC_RING_SIZE; i++) {
		if (sc->ether.tx_data[i] != NULL) {
			dev_kfree_skb_any(sc->ether.tx_data[i]);
			sc->ether.tx_data[i] = NULL;
		}
	}
}

void dc_drop_descriptor_list(struct channel *sc)
{
	u32 i;

	dc_clear_descriptor_list(sc);

	/* free all data buffers on RX ring */
	for (i = 0; i < SBE_2T3E3_RX_DESC_RING_SIZE; i++) {
		if (sc->ether.rx_data[i] != NULL) {
			dev_kfree_skb_any(sc->ether.rx_data[i]);
			sc->ether.rx_data[i] = NULL;
		}
	}

	kfree(sc->ether.rx_ring);
	sc->ether.rx_ring = NULL;
	kfree(sc->ether.tx_ring);
	sc->ether.tx_ring = NULL;
}


void dc_set_output_port(struct channel *sc)
{
	dc_clear_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
		      SBE_2T3E3_21143_VAL_PORT_SELECT);

	dc_write(sc->addr, SBE_2T3E3_21143_REG_SIA_STATUS, 0x00000301);
	dc_write(sc->addr, SBE_2T3E3_21143_REG_SIA_CONNECTIVITY, 0);
	dc_write(sc->addr, SBE_2T3E3_21143_REG_SIA_TRANSMIT_AND_RECEIVE, 0);
	dc_write(sc->addr, SBE_2T3E3_21143_REG_SIA_AND_GENERAL_PURPOSE_PORT, 0x08000011);

	dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
		   SBE_2T3E3_21143_VAL_TRANSMIT_THRESHOLD_MODE_100Mbs |
		   SBE_2T3E3_21143_VAL_HEARTBEAT_DISABLE |
		   SBE_2T3E3_21143_VAL_PORT_SELECT |
		   SBE_2T3E3_21143_VAL_FULL_DUPLEX_MODE);
}

void dc_restart(struct channel *sc)
{
	dev_warn(&sc->pdev->dev, "SBE 2T3E3: 21143 restart\n");

	dc_stop(sc);
	dc_reset(sc);
	dc_init(sc);	/* stop + reset + init */
	dc_start(sc);
}
