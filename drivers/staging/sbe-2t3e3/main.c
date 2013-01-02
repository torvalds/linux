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

#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include "2t3e3.h"

void t3e3_init(struct channel *sc)
{
	cpld_init(sc);
	dc_reset(sc);
	dc_init(sc);
	exar7250_init(sc);
	exar7300_init(sc);
}

int t3e3_if_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct channel *sc = dev_to_priv(dev);
	u32 current_write, last_write;
	unsigned long flags;
	struct sk_buff *skb2;

	if (skb == NULL) {
		sc->s.out_errors++;
		return 0;
	}

	if (sc->p.transmitter_on != SBE_2T3E3_ON) {
		sc->s.out_errors++;
		sc->s.out_dropped++;
		dev_kfree_skb_any(skb);
		return 0;
	}

	if (sc->s.OOF && sc->p.loopback == SBE_2T3E3_LOOPBACK_NONE) {
		sc->s.out_dropped++;
		dev_kfree_skb_any(skb);
		return 0;
	}

	spin_lock_irqsave(&sc->ether.tx_lock, flags);

	current_write = sc->ether.tx_ring_current_write;
	for (skb2 = skb; skb2 != NULL; skb2 = NULL) {
		if (skb2->len) {
			if ((sc->ether.tx_ring[current_write].tdes1 &
			     SBE_2T3E3_TX_DESC_BUFFER_1_SIZE) > 0)
				break;
			current_write = (current_write + 1) % SBE_2T3E3_TX_DESC_RING_SIZE;
			/*
			 * Leave at least 1 tx desc free so that dc_intr_tx() can
			 * identify empty list
			 */
			if (current_write == sc->ether.tx_ring_current_read)
				break;
		}
	}
	if (skb2 != NULL) {
		netif_stop_queue(sc->dev);
		sc->ether.tx_full = 1;
		dev_dbg(&sc->pdev->dev, "SBE 2T3E3: out of descriptors\n");
		spin_unlock_irqrestore(&sc->ether.tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	current_write = last_write = sc->ether.tx_ring_current_write;
	dev_dbg(&sc->pdev->dev, "sending mbuf (current_write = %d)\n",
		current_write);

	for (skb2 = skb; skb2 != NULL; skb2 = NULL) {
		if (skb2->len) {
			dev_dbg(&sc->pdev->dev,
				"sending mbuf (len = %d, next = %p)\n",
				skb2->len, NULL);

			sc->ether.tx_free_cnt--;
			sc->ether.tx_ring[current_write].tdes0 = 0;
			sc->ether.tx_ring[current_write].tdes1 &=
				SBE_2T3E3_TX_DESC_END_OF_RING |
				SBE_2T3E3_TX_DESC_SECOND_ADDRESS_CHAINED;
/* DISABLE_PADDING sometimes gets lost somehow, hands off... */
			sc->ether.tx_ring[current_write].tdes1 |=
				SBE_2T3E3_TX_DESC_DISABLE_PADDING | skb2->len;

			if (current_write == sc->ether.tx_ring_current_write) {
				sc->ether.tx_ring[current_write].tdes1 |=
					SBE_2T3E3_TX_DESC_FIRST_SEGMENT;
			} else {
				sc->ether.tx_ring[current_write].tdes0 =
					SBE_2T3E3_TX_DESC_21143_OWN;
			}

			sc->ether.tx_ring[current_write].tdes2 = virt_to_phys(skb2->data);
			sc->ether.tx_data[current_write] = NULL;

			last_write = current_write;
			current_write = (current_write + 1) % SBE_2T3E3_TX_DESC_RING_SIZE;
		}
	}

	sc->ether.tx_data[last_write] = skb;
	sc->ether.tx_ring[last_write].tdes1 |=
		SBE_2T3E3_TX_DESC_LAST_SEGMENT |
		SBE_2T3E3_TX_DESC_INTERRUPT_ON_COMPLETION;
	sc->ether.tx_ring[sc->ether.tx_ring_current_write].tdes0 |=
		SBE_2T3E3_TX_DESC_21143_OWN;
	sc->ether.tx_ring_current_write = current_write;

	dev_dbg(&sc->pdev->dev, "txput: tdes0 = %08X        tdes1 = %08X\n",
		sc->ether.tx_ring[last_write].tdes0,
		sc->ether.tx_ring[last_write].tdes1);

	dc_write(sc->addr, SBE_2T3E3_21143_REG_TRANSMIT_POLL_DEMAND,
		 0xffffffff);

	spin_unlock_irqrestore(&sc->ether.tx_lock, flags);
	return 0;
}


void t3e3_read_card_serial_number(struct channel *sc)
{
	u32 i;

	for (i = 0; i < 3; i++)
		sc->ether.card_serial_number[i] = t3e3_eeprom_read_word(sc, 10 + i);

	netdev_info(sc->dev, "SBE wanPMC-2T3E3 serial number: %04X%04X%04X\n",
		    sc->ether.card_serial_number[0],
		    sc->ether.card_serial_number[1],
		    sc->ether.card_serial_number[2]);
}

/*
  bit 0 led1 (green)
  bit 1 led1 (yellow)

  bit 2 led2 (green)
  bit 3 led2 (yellow)

  bit 4 led3 (green)
  bit 5 led3 (yellow)

  bit 6 led4 (green)
  bit 7 led4 (yellow)
*/

void update_led(struct channel *sc, int blinker)
{
	int leds;
	if (sc->s.LOS)
		leds = 0; /* led1 = off */
	else if (sc->s.OOF)
		leds = 2; /* led1 = yellow */
	else if ((blinker & 1) && sc->rcv_count) {
		leds = 0; /* led1 = off */
		sc->rcv_count = 0;
	} else
		leds = 1; /* led1 = green */
	cpld_write(sc, SBE_2T3E3_CPLD_REG_LEDR, leds);
	sc->leds = leds;
}
