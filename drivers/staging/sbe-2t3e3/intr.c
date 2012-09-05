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

#include <linux/hdlc.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include "2t3e3.h"

irqreturn_t t3e3_intr(int irq, void *dev_instance)
{
	struct channel *sc = dev_to_priv(dev_instance);
	u32 val;
	irqreturn_t ret = IRQ_NONE;

	sc->interrupt_active = 1;

	val = cpld_read(sc, SBE_2T3E3_CPLD_REG_PICSR);

	if (val & SBE_2T3E3_CPLD_VAL_RECEIVE_LOSS_OF_SIGNAL_CHANGE) {
		dev_dbg(&sc->pdev->dev,
			"Rx LOS Chng Int r=%02x (LOS|OOF=%02x)\n",
			val, (sc->s.LOS << 4) | sc->s.OOF);
		cpld_LOS_update(sc);
		ret = IRQ_HANDLED;
	}

	if (val & SBE_2T3E3_CPLD_VAL_INTERRUPT_FROM_ETHERNET_ASSERTED) {
		dc_intr(sc);
		ret = IRQ_HANDLED;
	}

	if (val & SBE_2T3E3_CPLD_VAL_INTERRUPT_FROM_FRAMER_ASSERTED) {
		exar7250_intr(sc);
		ret = IRQ_HANDLED;
	}

	/*
	  we don't care about other interrupt sources (DMO, LOS, LCV) because
	  they are handled by Framer too
	*/

	sc->interrupt_active = 0;
	return ret;
}

void dc_intr(struct channel *sc)
{
	u32 val;

	/* disable ethernet interrupts */
	/* grrr this clears interrupt summary bits !!! */
	dc_write(sc->addr, SBE_2T3E3_21143_REG_INTERRUPT_ENABLE, 0);

	while ((val = dc_read(sc->addr, SBE_2T3E3_21143_REG_STATUS)) &
	       (SBE_2T3E3_21143_VAL_RECEIVE_PROCESS_STOPPED |
		SBE_2T3E3_21143_VAL_RECEIVE_BUFFER_UNAVAILABLE |
		SBE_2T3E3_21143_VAL_RECEIVE_INTERRUPT |
		SBE_2T3E3_21143_VAL_TRANSMIT_UNDERFLOW |
		SBE_2T3E3_21143_VAL_TRANSMIT_BUFFER_UNAVAILABLE |
		SBE_2T3E3_21143_VAL_TRANSMIT_PROCESS_STOPPED |
		SBE_2T3E3_21143_VAL_TRANSMIT_INTERRUPT)) {
		dc_write(sc->addr, SBE_2T3E3_21143_REG_STATUS, val);

		dev_dbg(&sc->pdev->dev, "SBE 2T3E3: Ethernet controller interrupt! (CSR5 = %08X)\n",
			val);

		if (val & (SBE_2T3E3_21143_VAL_RECEIVE_INTERRUPT |
			   SBE_2T3E3_21143_VAL_RECEIVE_BUFFER_UNAVAILABLE |
			   SBE_2T3E3_21143_VAL_RECEIVE_PROCESS_STOPPED)) {
			if (val & SBE_2T3E3_21143_VAL_RECEIVE_INTERRUPT)
				dev_dbg(&sc->pdev->dev,
					"Receive interrupt (LOS=%d, OOF=%d)\n",
					sc->s.LOS, sc->s.OOF);
			if (val & SBE_2T3E3_21143_VAL_RECEIVE_BUFFER_UNAVAILABLE)
				dev_dbg(&sc->pdev->dev,
					"Receive buffer unavailable\n");
			if (val & SBE_2T3E3_21143_VAL_RECEIVE_PROCESS_STOPPED)
				dev_dbg(&sc->pdev->dev,
					"Receive process stopped\n");
			dc_intr_rx(sc);
		}

		if (val & SBE_2T3E3_21143_VAL_TRANSMIT_UNDERFLOW) {
			dev_dbg(&sc->pdev->dev, "Transmit underflow\n");
			dc_intr_tx_underflow(sc);
		}

		if (val & (SBE_2T3E3_21143_VAL_TRANSMIT_BUFFER_UNAVAILABLE |
			   SBE_2T3E3_21143_VAL_TRANSMIT_INTERRUPT |
			   SBE_2T3E3_21143_VAL_TRANSMIT_PROCESS_STOPPED)) {
			if (val & SBE_2T3E3_21143_VAL_TRANSMIT_INTERRUPT)
				dev_dbg(&sc->pdev->dev, "Transmit interrupt\n");
			if (val & SBE_2T3E3_21143_VAL_TRANSMIT_BUFFER_UNAVAILABLE)
				dev_dbg(&sc->pdev->dev,
					"Transmit buffer unavailable\n");
			if (val & SBE_2T3E3_21143_VAL_TRANSMIT_PROCESS_STOPPED)
				dev_dbg(&sc->pdev->dev,
					"Transmit process stopped\n");
			dc_intr_tx(sc);
		}
	}

	/* enable ethernet interrupts */
	dc_write(sc->addr, SBE_2T3E3_21143_REG_INTERRUPT_ENABLE,
		 sc->ether.interrupt_enable_mask);
}

void dc_intr_rx(struct channel *sc)
{
	u32 current_read;
	u32 error_mask, error;
	t3e3_rx_desc_t *current_desc;
	struct sk_buff *m, *m2;
	unsigned rcv_len;

	sc->rcv_count++; /* for the activity LED */

	current_read = sc->ether.rx_ring_current_read;
	dev_dbg(&sc->pdev->dev, "intr_rx current_read = %d\n", current_read);

	/* when ethernet loopback is set, ignore framer signals */
	if ((sc->p.loopback != SBE_2T3E3_LOOPBACK_ETHERNET) && sc->s.OOF) {
		while (!(sc->ether.rx_ring[current_read].rdes0 &
			 SBE_2T3E3_RX_DESC_21143_OWN)) {
			current_desc = &sc->ether.rx_ring[current_read];
			current_desc->rdes1 &= SBE_2T3E3_RX_DESC_END_OF_RING |
				SBE_2T3E3_RX_DESC_SECOND_ADDRESS_CHAINED;
			current_desc->rdes1 |= SBE_2T3E3_MTU;
			current_desc->rdes0 = SBE_2T3E3_RX_DESC_21143_OWN;
			current_read = (current_read + 1) % SBE_2T3E3_RX_DESC_RING_SIZE;
		}
		sc->ether.rx_ring_current_read = current_read;
		return;
	}

	while (!(sc->ether.rx_ring[current_read].rdes0 &
		 SBE_2T3E3_RX_DESC_21143_OWN)) {
		current_desc = &sc->ether.rx_ring[current_read];

		dev_dbg(&sc->pdev->dev, "rdes0: %08X        rdes1: %08X\n",
			current_desc->rdes0, current_desc->rdes1);

		m = sc->ether.rx_data[current_read];
		rcv_len = (current_desc->rdes0 & SBE_2T3E3_RX_DESC_FRAME_LENGTH) >>
			SBE_2T3E3_RX_DESC_FRAME_LENGTH_SHIFT;

		dev_dbg(&sc->pdev->dev, "mbuf was received (mbuf len = %d)\n",
			rcv_len);

		switch (sc->p.crc) {
		case SBE_2T3E3_CRC_16:
			rcv_len -= SBE_2T3E3_CRC16_LENGTH;
			break;
		case SBE_2T3E3_CRC_32:
			rcv_len -= SBE_2T3E3_CRC32_LENGTH;
			break;
		default:
			break;
		}

		if (current_desc->rdes0 & SBE_2T3E3_RX_DESC_LAST_DESC) {

			/* TODO: is collision possible? */
			error_mask = SBE_2T3E3_RX_DESC_DESC_ERROR |
				SBE_2T3E3_RX_DESC_COLLISION_SEEN |
				SBE_2T3E3_RX_DESC_DRIBBLING_BIT;

			switch (sc->p.frame_mode) {
			case SBE_2T3E3_FRAME_MODE_HDLC:
				error_mask |= SBE_2T3E3_RX_DESC_MII_ERROR;
				if (sc->p.crc == SBE_2T3E3_CRC_32)
					error_mask |= SBE_2T3E3_RX_DESC_CRC_ERROR;
				break;
			case SBE_2T3E3_FRAME_MODE_TRANSPARENT:
			case SBE_2T3E3_FRAME_MODE_RAW:
				break;
			default:
				error_mask = 0;
			}

			if (sc->s.LOS) {
				error_mask &= ~(SBE_2T3E3_RX_DESC_DRIBBLING_BIT |
						SBE_2T3E3_RX_DESC_MII_ERROR);
			}

			error = current_desc->rdes0 & error_mask;
			if (error) {
				sc->s.in_errors++;
				dev_dbg(&sc->pdev->dev,
					"error interrupt: NO_ERROR_MESSAGE = %d\n",
					sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES ? 1 : 0);

				current_desc->rdes1 &= SBE_2T3E3_RX_DESC_END_OF_RING |
					SBE_2T3E3_RX_DESC_SECOND_ADDRESS_CHAINED;
				current_desc->rdes1 |= SBE_2T3E3_MTU;
				current_desc->rdes0 = SBE_2T3E3_RX_DESC_21143_OWN;

				if (error & SBE_2T3E3_RX_DESC_DESC_ERROR) {
					if (!(sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES))
						dev_err(&sc->pdev->dev,
							"SBE 2T3E3: descriptor error\n");
					sc->s.in_error_desc++;
				}

				if (error & SBE_2T3E3_RX_DESC_COLLISION_SEEN) {
					if (!(sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES))
						dev_err(&sc->pdev->dev,
							"SBE 2T3E3: collision seen\n");
					sc->s.in_error_coll++;
				} else {
					if (error & SBE_2T3E3_RX_DESC_DRIBBLING_BIT) {
						if (!(sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES))
							dev_err(&sc->pdev->dev,
								"SBE 2T3E3: dribbling bits error\n");
						sc->s.in_error_drib++;
					}

					if (error & SBE_2T3E3_RX_DESC_CRC_ERROR) {
						if (!(sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES))
							dev_err(&sc->pdev->dev,
								"SBE 2T3E3: crc error\n");
						sc->s.in_error_crc++;
					}
				}

				if (error & SBE_2T3E3_RX_DESC_MII_ERROR) {
					if (!(sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES))
						dev_err(&sc->pdev->dev, "SBE 2T3E3: mii error\n");
					sc->s.in_error_mii++;
				}

				current_read = (current_read + 1) % SBE_2T3E3_RX_DESC_RING_SIZE;
				sc->r.flags |= SBE_2T3E3_FLAG_NO_ERROR_MESSAGES;
				continue;
			}
		}

		current_desc->rdes1 &= SBE_2T3E3_RX_DESC_END_OF_RING |
			SBE_2T3E3_RX_DESC_SECOND_ADDRESS_CHAINED;
		current_desc->rdes1 |= SBE_2T3E3_MTU;

		if (rcv_len > 1600) {
			sc->s.in_errors++;
			sc->s.in_dropped++;
			if (!(sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES))
				dev_err(&sc->pdev->dev, "SBE 2T3E3: oversized rx: rdes0 = %08X\n",
					current_desc->rdes0);
		} else {
			m2 = dev_alloc_skb(MCLBYTES);
			if (m2 != NULL) {
				current_desc->rdes2 = virt_to_phys(m2->data);
				sc->ether.rx_data[current_read] = m2;
				sc->s.in_packets++;
				sc->s.in_bytes += rcv_len;
				m->dev = sc->dev;
				skb_put(m, rcv_len);
				skb_reset_mac_header(m);
				m->protocol = hdlc_type_trans(m, m->dev);
				netif_rx(m);

				/* good packet was received so we will show error messages again... */
				if (sc->r.flags & SBE_2T3E3_FLAG_NO_ERROR_MESSAGES) {
					dev_dbg(&sc->pdev->dev,
						"setting ERROR_MESSAGES->0\n");
					sc->r.flags &= ~SBE_2T3E3_FLAG_NO_ERROR_MESSAGES;
				}

			} else {
				sc->s.in_errors++;
				sc->s.in_dropped++;
			}
		}
		current_desc->rdes0 = SBE_2T3E3_RX_DESC_21143_OWN;
		current_read = (current_read + 1) % SBE_2T3E3_RX_DESC_RING_SIZE;
	}

	sc->ether.rx_ring_current_read = current_read;

	dc_write(sc->addr, SBE_2T3E3_21143_REG_RECEIVE_POLL_DEMAND, 0xFFFFFFFF);
}

void dc_intr_tx(struct channel *sc)
{
	u32 current_read, current_write;
	u32 last_segment, error;
	t3e3_tx_desc_t *current_desc;

	spin_lock(&sc->ether.tx_lock);

	current_read = sc->ether.tx_ring_current_read;
	current_write = sc->ether.tx_ring_current_write;

	while (current_read != current_write) {
		current_desc = &sc->ether.tx_ring[current_read];

		if (current_desc->tdes0 & SBE_2T3E3_RX_DESC_21143_OWN)
			break;

		dev_dbg(&sc->pdev->dev,
			"txeof: tdes0 = %08X        tdes1 = %08X\n",
			current_desc->tdes0, current_desc->tdes1);

		error = current_desc->tdes0 & (SBE_2T3E3_TX_DESC_ERROR_SUMMARY |
					       SBE_2T3E3_TX_DESC_TRANSMIT_JABBER_TIMEOUT |
					       SBE_2T3E3_TX_DESC_LOSS_OF_CARRIER |
					       SBE_2T3E3_TX_DESC_NO_CARRIER |
					       SBE_2T3E3_TX_DESC_LINK_FAIL_REPORT |
					       SBE_2T3E3_TX_DESC_UNDERFLOW_ERROR |
					       SBE_2T3E3_TX_DESC_DEFFERED);

		last_segment = current_desc->tdes1 & SBE_2T3E3_TX_DESC_LAST_SEGMENT;

		current_desc->tdes0 = 0;
		current_desc->tdes1 &= SBE_2T3E3_TX_DESC_END_OF_RING |
			SBE_2T3E3_TX_DESC_SECOND_ADDRESS_CHAINED;
		current_desc->tdes2 = 0;
		sc->ether.tx_free_cnt++;

		if (last_segment != SBE_2T3E3_TX_DESC_LAST_SEGMENT) {
			current_read = (current_read + 1) % SBE_2T3E3_TX_DESC_RING_SIZE;
			continue;
		}


		if (sc->ether.tx_data[current_read]) {
			sc->s.out_packets++;
			sc->s.out_bytes += sc->ether.tx_data[current_read]->len;
			dev_kfree_skb_any(sc->ether.tx_data[current_read]);
			sc->ether.tx_data[current_read] = NULL;
		}

		if (error > 0) {
			sc->s.out_errors++;

			if (error & SBE_2T3E3_TX_DESC_TRANSMIT_JABBER_TIMEOUT) {
				dev_err(&sc->pdev->dev, "SBE 2T3E3: transmit jabber timeout\n");
				sc->s.out_error_jab++;
			}

			if (sc->p.loopback != SBE_2T3E3_LOOPBACK_ETHERNET) {
				if (error & SBE_2T3E3_TX_DESC_LOSS_OF_CARRIER) {
					dev_err(&sc->pdev->dev, "SBE 2T3E3: loss of carrier\n");
					sc->s.out_error_lost_carr++;
				}

				if (error & SBE_2T3E3_TX_DESC_NO_CARRIER) {
					dev_err(&sc->pdev->dev, "SBE 2T3E3: no carrier\n");
					sc->s.out_error_no_carr++;
				}
			}

			if (error & SBE_2T3E3_TX_DESC_LINK_FAIL_REPORT) {
				dev_err(&sc->pdev->dev, "SBE 2T3E3: link fail report\n");
				sc->s.out_error_link_fail++;
			}

			if (error & SBE_2T3E3_TX_DESC_UNDERFLOW_ERROR) {
				dev_err(&sc->pdev->dev, "SBE 2T3E3:"
					" transmission underflow error\n");
				sc->s.out_error_underflow++;
				spin_unlock(&sc->ether.tx_lock);

				dc_restart(sc);
				return;
			}

			if (error & SBE_2T3E3_TX_DESC_DEFFERED) {
				dev_err(&sc->pdev->dev, "SBE 2T3E3: transmission deferred\n");
				sc->s.out_error_dereferred++;
			}
		}

		current_read = (current_read + 1) % SBE_2T3E3_TX_DESC_RING_SIZE;
	}

	sc->ether.tx_ring_current_read = current_read;

	/* Relieve flow control when the TX queue is drained at least half way */
	if (sc->ether.tx_full &&
	    (sc->ether.tx_free_cnt >= (SBE_2T3E3_TX_DESC_RING_SIZE / 2))) {
		sc->ether.tx_full = 0;
		netif_wake_queue(sc->dev);
	}
	spin_unlock(&sc->ether.tx_lock);
}


void dc_intr_tx_underflow(struct channel *sc)
{
	u32 val;

	dc_transmitter_onoff(sc, SBE_2T3E3_OFF);

	val = dc_read(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE);
	dc_clear_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
		      SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS);

	switch (val & SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS) {
	case SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_1:
		dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			    SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_2);
		break;
	case SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_2:
		dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			    SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_3);
		break;
	case SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_3:
		dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			    SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_4);
		break;
	case SBE_2T3E3_21143_VAL_THRESHOLD_CONTROL_BITS_4:
	default:
		dc_set_bits(sc->addr, SBE_2T3E3_21143_REG_OPERATION_MODE,
			    SBE_2T3E3_21143_VAL_STORE_AND_FORWARD);
		break;
	}

	dc_transmitter_onoff(sc, SBE_2T3E3_ON);
}




void exar7250_intr(struct channel *sc)
{
	u32 status, old_OOF;

	old_OOF = sc->s.OOF;

	status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_BLOCK_INTERRUPT_STATUS);
	dev_dbg(&sc->pdev->dev, "SBE 2T3E3: Framer interrupt! (REG[0x05] = %02X)\n", status);

	switch (sc->p.frame_type) {
	case SBE_2T3E3_FRAME_TYPE_E3_G751:
	case SBE_2T3E3_FRAME_TYPE_E3_G832:
		exar7250_E3_intr(sc, status);
		break;

	case SBE_2T3E3_FRAME_TYPE_T3_CBIT:
	case SBE_2T3E3_FRAME_TYPE_T3_M13:
		exar7250_T3_intr(sc, status);
		break;

	default:
		break;
	}

	if (sc->s.OOF != old_OOF) {
		if (sc->s.OOF) {
			if (sc->p.loopback == SBE_2T3E3_LOOPBACK_NONE) {
				dev_dbg(&sc->pdev->dev, "SBE 2T3E3: Disabling eth interrupts\n");
				/* turn off ethernet interrupts */
				dc_stop_intr(sc);
			}
		} else if (sc->r.flags & SBE_2T3E3_FLAG_NETWORK_UP) {
			dev_dbg(&sc->pdev->dev, "SBE 2T3E3: Enabling eth interrupts\n");
			/* start interrupts */
			sc->s.OOF = 1;
			dc_intr_rx(sc);
			sc->s.OOF = 0;
			if (sc->p.receiver_on) {
				dc_receiver_onoff(sc, SBE_2T3E3_OFF);
				dc_receiver_onoff(sc, SBE_2T3E3_ON);
			}
			dc_start_intr(sc);
		}
	}
}


void exar7250_T3_intr(struct channel *sc, u32 block_status)
{
	u32 status, result;

	if (block_status & SBE_2T3E3_FRAMER_VAL_RX_INTERRUPT_STATUS) {
		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_INTERRUPT_STATUS);

		if (status) {
			dev_dbg(&sc->pdev->dev,
				"Framer interrupt T3 RX (REG[0x13] = %02X)\n",
				status);

			result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_CONFIGURATION_STATUS);

			cpld_LOS_update(sc);

			if (status & SBE_2T3E3_FRAMER_VAL_T3_RX_OOF_INTERRUPT_STATUS) {
				sc->s.OOF = result & SBE_2T3E3_FRAMER_VAL_T3_RX_OOF ? 1 : 0;
				dev_dbg(&sc->pdev->dev,
					"Framer interrupt T3: OOF (%d)\n",
					sc->s.OOF);
			}

			exar7250_write(sc, SBE_2T3E3_FRAMER_REG_T3_RX_INTERRUPT_ENABLE,
				       SBE_2T3E3_FRAMER_VAL_T3_RX_LOS_INTERRUPT_ENABLE |
				       SBE_2T3E3_FRAMER_VAL_T3_RX_OOF_INTERRUPT_ENABLE);
				}

		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_FEAC_INTERRUPT_ENABLE_STATUS);
		if (status) {
			dev_dbg(&sc->pdev->dev,
				"Framer interrupt T3 RX (REG[0x17] = %02X)\n",
				status);
		}

		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_RX_LAPD_CONTROL);
		if (status)
			dev_dbg(&sc->pdev->dev,
				"Framer interrupt T3 RX (REG[0x18] = %02X)\n",
				status);
	}


	if (block_status & SBE_2T3E3_FRAMER_VAL_TX_INTERRUPT_STATUS) {
		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_TX_FEAC_CONFIGURATION_STATUS);
		dev_dbg(&sc->pdev->dev, "SBE 2T3E3: Framer interrupt T3 TX (REG[0x31] = %02X)\n",
			status);

		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_T3_TX_LAPD_STATUS);
		dev_dbg(&sc->pdev->dev, "SBE 2T3E3: Framer interrupt T3 TX (REG[0x34] = %02X)\n",
			status);
	}
}


void exar7250_E3_intr(struct channel *sc, u32 block_status)
{
	u32 status, result;

	if (block_status & SBE_2T3E3_FRAMER_VAL_RX_INTERRUPT_STATUS) {
		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_E3_RX_INTERRUPT_STATUS_1);

		if (status) {
			dev_dbg(&sc->pdev->dev,
				"Framer interrupt E3 RX (REG[0x14] = %02X)\n",
				status);

			result = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_E3_RX_CONFIGURATION_STATUS_2);

			cpld_LOS_update(sc);

			if (status & SBE_2T3E3_FRAMER_VAL_E3_RX_OOF_INTERRUPT_STATUS) {
				sc->s.OOF = result & SBE_2T3E3_FRAMER_VAL_E3_RX_OOF ? 1 : 0;
				dev_dbg(&sc->pdev->dev,
					"Framer interrupt E3: OOF (%d)\n",
					sc->s.OOF);
			}

			exar7250_write(sc, SBE_2T3E3_FRAMER_REG_E3_RX_INTERRUPT_ENABLE_1,
				       SBE_2T3E3_FRAMER_VAL_E3_RX_OOF_INTERRUPT_ENABLE |
				       SBE_2T3E3_FRAMER_VAL_E3_RX_LOS_INTERRUPT_ENABLE
				);
				}

		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_E3_RX_INTERRUPT_STATUS_2);
		if (status) {
			dev_dbg(&sc->pdev->dev,
				"Framer interrupt E3 RX (REG[0x15] = %02X)\n",
				status);

		}

	}

	if (block_status & SBE_2T3E3_FRAMER_VAL_TX_INTERRUPT_STATUS) {
		status = exar7250_read(sc, SBE_2T3E3_FRAMER_REG_E3_TX_LAPD_STATUS);
		dev_dbg(&sc->pdev->dev, "SBE 2T3E3: Framer interrupt E3 TX (REG[0x34] = %02X)\n",
			status);
	}
}
