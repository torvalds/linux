/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*************************************\
* DMA and interrupt masking functions *
\*************************************/

/*
 * dma.c - DMA and interrupt masking functions
 *
 * Here we setup descriptor pointers (rxdp/txdp) start/stop dma engine and
 * handle queue setup for 5210 chipset (rest are handled on qcu.c).
 * Also we setup interrupt mask register (IMR) and read the various iterrupt
 * status registers (ISR).
 *
 * TODO: Handle SISR on 5211+ and introduce a function to return the queue
 * number that resulted the interrupt.
 */

#include "ath5k.h"
#include "reg.h"
#include "debug.h"
#include "base.h"


/*********\
* Receive *
\*********/

/**
 * ath5k_hw_start_rx_dma - Start DMA receive
 *
 * @ah:	The &struct ath5k_hw
 */
void ath5k_hw_start_rx_dma(struct ath5k_hw *ah)
{
	ath5k_hw_reg_write(ah, AR5K_CR_RXE, AR5K_CR);
	ath5k_hw_reg_read(ah, AR5K_CR);
}

/**
 * ath5k_hw_stop_rx_dma - Stop DMA receive
 *
 * @ah:	The &struct ath5k_hw
 */
static int ath5k_hw_stop_rx_dma(struct ath5k_hw *ah)
{
	unsigned int i;

	ath5k_hw_reg_write(ah, AR5K_CR_RXD, AR5K_CR);

	/*
	 * It may take some time to disable the DMA receive unit
	 */
	for (i = 1000; i > 0 &&
			(ath5k_hw_reg_read(ah, AR5K_CR) & AR5K_CR_RXE) != 0;
			i--)
		udelay(100);

	if (!i)
		ATH5K_DBG(ah->ah_sc, ATH5K_DEBUG_DMA,
				"failed to stop RX DMA !\n");

	return i ? 0 : -EBUSY;
}

/**
 * ath5k_hw_get_rxdp - Get RX Descriptor's address
 *
 * @ah: The &struct ath5k_hw
 */
u32 ath5k_hw_get_rxdp(struct ath5k_hw *ah)
{
	return ath5k_hw_reg_read(ah, AR5K_RXDP);
}

/**
 * ath5k_hw_set_rxdp - Set RX Descriptor's address
 *
 * @ah: The &struct ath5k_hw
 * @phys_addr: RX descriptor address
 *
 * Returns -EIO if rx is active
 */
int ath5k_hw_set_rxdp(struct ath5k_hw *ah, u32 phys_addr)
{
	if (ath5k_hw_reg_read(ah, AR5K_CR) & AR5K_CR_RXE) {
		ATH5K_DBG(ah->ah_sc, ATH5K_DEBUG_DMA,
				"tried to set RXDP while rx was active !\n");
		return -EIO;
	}

	ath5k_hw_reg_write(ah, phys_addr, AR5K_RXDP);
	return 0;
}


/**********\
* Transmit *
\**********/

/**
 * ath5k_hw_start_tx_dma - Start DMA transmit for a specific queue
 *
 * @ah: The &struct ath5k_hw
 * @queue: The hw queue number
 *
 * Start DMA transmit for a specific queue and since 5210 doesn't have
 * QCU/DCU, set up queue parameters for 5210 here based on queue type (one
 * queue for normal data and one queue for beacons). For queue setup
 * on newer chips check out qcu.c. Returns -EINVAL if queue number is out
 * of range or if queue is already disabled.
 *
 * NOTE: Must be called after setting up tx control descriptor for that
 * queue (see below).
 */
int ath5k_hw_start_tx_dma(struct ath5k_hw *ah, unsigned int queue)
{
	u32 tx_queue;

	AR5K_ASSERT_ENTRY(queue, ah->ah_capabilities.cap_queues.q_tx_num);

	/* Return if queue is declared inactive */
	if (ah->ah_txq[queue].tqi_type == AR5K_TX_QUEUE_INACTIVE)
		return -EINVAL;

	if (ah->ah_version == AR5K_AR5210) {
		tx_queue = ath5k_hw_reg_read(ah, AR5K_CR);

		/*
		 * Set the queue by type on 5210
		 */
		switch (ah->ah_txq[queue].tqi_type) {
		case AR5K_TX_QUEUE_DATA:
			tx_queue |= AR5K_CR_TXE0 & ~AR5K_CR_TXD0;
			break;
		case AR5K_TX_QUEUE_BEACON:
			tx_queue |= AR5K_CR_TXE1 & ~AR5K_CR_TXD1;
			ath5k_hw_reg_write(ah, AR5K_BCR_TQ1V | AR5K_BCR_BDMAE,
					AR5K_BSR);
			break;
		case AR5K_TX_QUEUE_CAB:
			tx_queue |= AR5K_CR_TXE1 & ~AR5K_CR_TXD1;
			ath5k_hw_reg_write(ah, AR5K_BCR_TQ1FV | AR5K_BCR_TQ1V |
				AR5K_BCR_BDMAE, AR5K_BSR);
			break;
		default:
			return -EINVAL;
		}
		/* Start queue */
		ath5k_hw_reg_write(ah, tx_queue, AR5K_CR);
		ath5k_hw_reg_read(ah, AR5K_CR);
	} else {
		/* Return if queue is disabled */
		if (AR5K_REG_READ_Q(ah, AR5K_QCU_TXD, queue))
			return -EIO;

		/* Start queue */
		AR5K_REG_WRITE_Q(ah, AR5K_QCU_TXE, queue);
	}

	return 0;
}

/**
 * ath5k_hw_stop_tx_dma - Stop DMA transmit on a specific queue
 *
 * @ah: The &struct ath5k_hw
 * @queue: The hw queue number
 *
 * Stop DMA transmit on a specific hw queue and drain queue so we don't
 * have any pending frames. Returns -EBUSY if we still have pending frames,
 * -EINVAL if queue number is out of range or inactive.
 *
 */
static int ath5k_hw_stop_tx_dma(struct ath5k_hw *ah, unsigned int queue)
{
	unsigned int i = 40;
	u32 tx_queue, pending;

	AR5K_ASSERT_ENTRY(queue, ah->ah_capabilities.cap_queues.q_tx_num);

	/* Return if queue is declared inactive */
	if (ah->ah_txq[queue].tqi_type == AR5K_TX_QUEUE_INACTIVE)
		return -EINVAL;

	if (ah->ah_version == AR5K_AR5210) {
		tx_queue = ath5k_hw_reg_read(ah, AR5K_CR);

		/*
		 * Set by queue type
		 */
		switch (ah->ah_txq[queue].tqi_type) {
		case AR5K_TX_QUEUE_DATA:
			tx_queue |= AR5K_CR_TXD0 & ~AR5K_CR_TXE0;
			break;
		case AR5K_TX_QUEUE_BEACON:
		case AR5K_TX_QUEUE_CAB:
			/* XXX Fix me... */
			tx_queue |= AR5K_CR_TXD1 & ~AR5K_CR_TXD1;
			ath5k_hw_reg_write(ah, 0, AR5K_BSR);
			break;
		default:
			return -EINVAL;
		}

		/* Stop queue */
		ath5k_hw_reg_write(ah, tx_queue, AR5K_CR);
		ath5k_hw_reg_read(ah, AR5K_CR);
	} else {

		/*
		 * Enable DCU early termination to quickly
		 * flush any pending frames from QCU
		 */
		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
					AR5K_QCU_MISC_DCU_EARLY);

		/*
		 * Schedule TX disable and wait until queue is empty
		 */
		AR5K_REG_WRITE_Q(ah, AR5K_QCU_TXD, queue);

		/* Wait for queue to stop */
		for (i = 1000; i > 0 &&
		(AR5K_REG_READ_Q(ah, AR5K_QCU_TXE, queue) != 0);
		i--)
			udelay(100);

		if (AR5K_REG_READ_Q(ah, AR5K_QCU_TXE, queue))
			ATH5K_DBG(ah->ah_sc, ATH5K_DEBUG_DMA,
				"queue %i didn't stop !\n", queue);

		/* Check for pending frames */
		i = 1000;
		do {
			pending = ath5k_hw_reg_read(ah,
				AR5K_QUEUE_STATUS(queue)) &
				AR5K_QCU_STS_FRMPENDCNT;
			udelay(100);
		} while (--i && pending);

		/* For 2413+ order PCU to drop packets using
		 * QUIET mechanism */
		if (ah->ah_mac_version >= (AR5K_SREV_AR2414 >> 4) &&
		pending){
			/* Set periodicity and duration */
			ath5k_hw_reg_write(ah,
				AR5K_REG_SM(100, AR5K_QUIET_CTL2_QT_PER)|
				AR5K_REG_SM(10, AR5K_QUIET_CTL2_QT_DUR),
				AR5K_QUIET_CTL2);

			/* Enable quiet period for current TSF */
			ath5k_hw_reg_write(ah,
				AR5K_QUIET_CTL1_QT_EN |
				AR5K_REG_SM(ath5k_hw_reg_read(ah,
						AR5K_TSF_L32_5211) >> 10,
						AR5K_QUIET_CTL1_NEXT_QT_TSF),
				AR5K_QUIET_CTL1);

			/* Force channel idle high */
			AR5K_REG_ENABLE_BITS(ah, AR5K_DIAG_SW_5211,
					AR5K_DIAG_SW_CHANNEL_IDLE_HIGH);

			/* Wait a while and disable mechanism */
			udelay(400);
			AR5K_REG_DISABLE_BITS(ah, AR5K_QUIET_CTL1,
						AR5K_QUIET_CTL1_QT_EN);

			/* Re-check for pending frames */
			i = 100;
			do {
				pending = ath5k_hw_reg_read(ah,
					AR5K_QUEUE_STATUS(queue)) &
					AR5K_QCU_STS_FRMPENDCNT;
				udelay(100);
			} while (--i && pending);

			AR5K_REG_DISABLE_BITS(ah, AR5K_DIAG_SW_5211,
					AR5K_DIAG_SW_CHANNEL_IDLE_HIGH);

			if (pending)
				ATH5K_DBG(ah->ah_sc, ATH5K_DEBUG_DMA,
					"quiet mechanism didn't work q:%i !\n",
					queue);
		}

		/*
		 * Disable DCU early termination
		 */
		AR5K_REG_DISABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
					AR5K_QCU_MISC_DCU_EARLY);

		/* Clear register */
		ath5k_hw_reg_write(ah, 0, AR5K_QCU_TXD);
		if (pending) {
			ATH5K_DBG(ah->ah_sc, ATH5K_DEBUG_DMA,
					"tx dma didn't stop (q:%i, frm:%i) !\n",
					queue, pending);
			return -EBUSY;
		}
	}

	/* TODO: Check for success on 5210 else return error */
	return 0;
}

/**
 * ath5k_hw_stop_beacon_queue - Stop beacon queue
 *
 * @ah The &struct ath5k_hw
 * @queue The queue number
 *
 * Returns -EIO if queue didn't stop
 */
int ath5k_hw_stop_beacon_queue(struct ath5k_hw *ah, unsigned int queue)
{
	int ret;
	ret = ath5k_hw_stop_tx_dma(ah, queue);
	if (ret) {
		ATH5K_DBG(ah->ah_sc, ATH5K_DEBUG_DMA,
				"beacon queue didn't stop !\n");
		return -EIO;
	}
	return 0;
}

/**
 * ath5k_hw_get_txdp - Get TX Descriptor's address for a specific queue
 *
 * @ah: The &struct ath5k_hw
 * @queue: The hw queue number
 *
 * Get TX descriptor's address for a specific queue. For 5210 we ignore
 * the queue number and use tx queue type since we only have 2 queues.
 * We use TXDP0 for normal data queue and TXDP1 for beacon queue.
 * For newer chips with QCU/DCU we just read the corresponding TXDP register.
 *
 * XXX: Is TXDP read and clear ?
 */
u32 ath5k_hw_get_txdp(struct ath5k_hw *ah, unsigned int queue)
{
	u16 tx_reg;

	AR5K_ASSERT_ENTRY(queue, ah->ah_capabilities.cap_queues.q_tx_num);

	/*
	 * Get the transmit queue descriptor pointer from the selected queue
	 */
	/*5210 doesn't have QCU*/
	if (ah->ah_version == AR5K_AR5210) {
		switch (ah->ah_txq[queue].tqi_type) {
		case AR5K_TX_QUEUE_DATA:
			tx_reg = AR5K_NOQCU_TXDP0;
			break;
		case AR5K_TX_QUEUE_BEACON:
		case AR5K_TX_QUEUE_CAB:
			tx_reg = AR5K_NOQCU_TXDP1;
			break;
		default:
			return 0xffffffff;
		}
	} else {
		tx_reg = AR5K_QUEUE_TXDP(queue);
	}

	return ath5k_hw_reg_read(ah, tx_reg);
}

/**
 * ath5k_hw_set_txdp - Set TX Descriptor's address for a specific queue
 *
 * @ah: The &struct ath5k_hw
 * @queue: The hw queue number
 *
 * Set TX descriptor's address for a specific queue. For 5210 we ignore
 * the queue number and we use tx queue type since we only have 2 queues
 * so as above we use TXDP0 for normal data queue and TXDP1 for beacon queue.
 * For newer chips with QCU/DCU we just set the corresponding TXDP register.
 * Returns -EINVAL if queue type is invalid for 5210 and -EIO if queue is still
 * active.
 */
int ath5k_hw_set_txdp(struct ath5k_hw *ah, unsigned int queue, u32 phys_addr)
{
	u16 tx_reg;

	AR5K_ASSERT_ENTRY(queue, ah->ah_capabilities.cap_queues.q_tx_num);

	/*
	 * Set the transmit queue descriptor pointer register by type
	 * on 5210
	 */
	if (ah->ah_version == AR5K_AR5210) {
		switch (ah->ah_txq[queue].tqi_type) {
		case AR5K_TX_QUEUE_DATA:
			tx_reg = AR5K_NOQCU_TXDP0;
			break;
		case AR5K_TX_QUEUE_BEACON:
		case AR5K_TX_QUEUE_CAB:
			tx_reg = AR5K_NOQCU_TXDP1;
			break;
		default:
			return -EINVAL;
		}
	} else {
		/*
		 * Set the transmit queue descriptor pointer for
		 * the selected queue on QCU for 5211+
		 * (this won't work if the queue is still active)
		 */
		if (AR5K_REG_READ_Q(ah, AR5K_QCU_TXE, queue))
			return -EIO;

		tx_reg = AR5K_QUEUE_TXDP(queue);
	}

	/* Set descriptor pointer */
	ath5k_hw_reg_write(ah, phys_addr, tx_reg);

	return 0;
}

/**
 * ath5k_hw_update_tx_triglevel - Update tx trigger level
 *
 * @ah: The &struct ath5k_hw
 * @increase: Flag to force increase of trigger level
 *
 * This function increases/decreases the tx trigger level for the tx fifo
 * buffer (aka FIFO threshold) that is used to indicate when PCU flushes
 * the buffer and transmits its data. Lowering this results sending small
 * frames more quickly but can lead to tx underruns, raising it a lot can
 * result other problems (i think bmiss is related). Right now we start with
 * the lowest possible (64Bytes) and if we get tx underrun we increase it using
 * the increase flag. Returns -EIO if we have reached maximum/minimum.
 *
 * XXX: Link this with tx DMA size ?
 * XXX: Use it to save interrupts ?
 * TODO: Needs testing, i think it's related to bmiss...
 */
int ath5k_hw_update_tx_triglevel(struct ath5k_hw *ah, bool increase)
{
	u32 trigger_level, imr;
	int ret = -EIO;

	/*
	 * Disable interrupts by setting the mask
	 */
	imr = ath5k_hw_set_imr(ah, ah->ah_imr & ~AR5K_INT_GLOBAL);

	trigger_level = AR5K_REG_MS(ath5k_hw_reg_read(ah, AR5K_TXCFG),
			AR5K_TXCFG_TXFULL);

	if (!increase) {
		if (--trigger_level < AR5K_TUNE_MIN_TX_FIFO_THRES)
			goto done;
	} else
		trigger_level +=
			((AR5K_TUNE_MAX_TX_FIFO_THRES - trigger_level) / 2);

	/*
	 * Update trigger level on success
	 */
	if (ah->ah_version == AR5K_AR5210)
		ath5k_hw_reg_write(ah, trigger_level, AR5K_TRIG_LVL);
	else
		AR5K_REG_WRITE_BITS(ah, AR5K_TXCFG,
				AR5K_TXCFG_TXFULL, trigger_level);

	ret = 0;

done:
	/*
	 * Restore interrupt mask
	 */
	ath5k_hw_set_imr(ah, imr);

	return ret;
}


/*******************\
* Interrupt masking *
\*******************/

/**
 * ath5k_hw_is_intr_pending - Check if we have pending interrupts
 *
 * @ah: The &struct ath5k_hw
 *
 * Check if we have pending interrupts to process. Returns 1 if we
 * have pending interrupts and 0 if we haven't.
 */
bool ath5k_hw_is_intr_pending(struct ath5k_hw *ah)
{
	return ath5k_hw_reg_read(ah, AR5K_INTPEND) == 1 ? 1 : 0;
}

/**
 * ath5k_hw_get_isr - Get interrupt status
 *
 * @ah: The @struct ath5k_hw
 * @interrupt_mask: Driver's interrupt mask used to filter out
 * interrupts in sw.
 *
 * This function is used inside our interrupt handler to determine the reason
 * for the interrupt by reading Primary Interrupt Status Register. Returns an
 * abstract interrupt status mask which is mostly ISR with some uncommon bits
 * being mapped on some standard non hw-specific positions
 * (check out &ath5k_int).
 *
 * NOTE: We use read-and-clear register, so after this function is called ISR
 * is zeroed.
 */
int ath5k_hw_get_isr(struct ath5k_hw *ah, enum ath5k_int *interrupt_mask)
{
	u32 data;

	/*
	 * Read interrupt status from the Interrupt Status register
	 * on 5210
	 */
	if (ah->ah_version == AR5K_AR5210) {
		data = ath5k_hw_reg_read(ah, AR5K_ISR);
		if (unlikely(data == AR5K_INT_NOCARD)) {
			*interrupt_mask = data;
			return -ENODEV;
		}
	} else {
		/*
		 * Read interrupt status from Interrupt
		 * Status Register shadow copy (Read And Clear)
		 *
		 * Note: PISR/SISR Not available on 5210
		 */
		data = ath5k_hw_reg_read(ah, AR5K_RAC_PISR);
		if (unlikely(data == AR5K_INT_NOCARD)) {
			*interrupt_mask = data;
			return -ENODEV;
		}
	}

	/*
	 * Get abstract interrupt mask (driver-compatible)
	 */
	*interrupt_mask = (data & AR5K_INT_COMMON) & ah->ah_imr;

	if (ah->ah_version != AR5K_AR5210) {
		u32 sisr2 = ath5k_hw_reg_read(ah, AR5K_RAC_SISR2);

		/*HIU = Host Interface Unit (PCI etc)*/
		if (unlikely(data & (AR5K_ISR_HIUERR)))
			*interrupt_mask |= AR5K_INT_FATAL;

		/*Beacon Not Ready*/
		if (unlikely(data & (AR5K_ISR_BNR)))
			*interrupt_mask |= AR5K_INT_BNR;

		if (unlikely(sisr2 & (AR5K_SISR2_SSERR |
					AR5K_SISR2_DPERR |
					AR5K_SISR2_MCABT)))
			*interrupt_mask |= AR5K_INT_FATAL;

		if (data & AR5K_ISR_TIM)
			*interrupt_mask |= AR5K_INT_TIM;

		if (data & AR5K_ISR_BCNMISC) {
			if (sisr2 & AR5K_SISR2_TIM)
				*interrupt_mask |= AR5K_INT_TIM;
			if (sisr2 & AR5K_SISR2_DTIM)
				*interrupt_mask |= AR5K_INT_DTIM;
			if (sisr2 & AR5K_SISR2_DTIM_SYNC)
				*interrupt_mask |= AR5K_INT_DTIM_SYNC;
			if (sisr2 & AR5K_SISR2_BCN_TIMEOUT)
				*interrupt_mask |= AR5K_INT_BCN_TIMEOUT;
			if (sisr2 & AR5K_SISR2_CAB_TIMEOUT)
				*interrupt_mask |= AR5K_INT_CAB_TIMEOUT;
		}

		if (data & AR5K_ISR_RXDOPPLER)
			*interrupt_mask |= AR5K_INT_RX_DOPPLER;
		if (data & AR5K_ISR_QCBRORN) {
			*interrupt_mask |= AR5K_INT_QCBRORN;
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR3),
					AR5K_SISR3_QCBRORN);
		}
		if (data & AR5K_ISR_QCBRURN) {
			*interrupt_mask |= AR5K_INT_QCBRURN;
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR3),
					AR5K_SISR3_QCBRURN);
		}
		if (data & AR5K_ISR_QTRIG) {
			*interrupt_mask |= AR5K_INT_QTRIG;
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR4),
					AR5K_SISR4_QTRIG);
		}

		if (data & AR5K_ISR_TXOK)
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR0),
					AR5K_SISR0_QCU_TXOK);

		if (data & AR5K_ISR_TXDESC)
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR0),
					AR5K_SISR0_QCU_TXDESC);

		if (data & AR5K_ISR_TXERR)
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR1),
					AR5K_SISR1_QCU_TXERR);

		if (data & AR5K_ISR_TXEOL)
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR1),
					AR5K_SISR1_QCU_TXEOL);

		if (data & AR5K_ISR_TXURN)
			ah->ah_txq_isr |= AR5K_REG_MS(
					ath5k_hw_reg_read(ah, AR5K_RAC_SISR2),
					AR5K_SISR2_QCU_TXURN);
	} else {
		if (unlikely(data & (AR5K_ISR_SSERR | AR5K_ISR_MCABT
				| AR5K_ISR_HIUERR | AR5K_ISR_DPERR)))
			*interrupt_mask |= AR5K_INT_FATAL;

		/*
		 * XXX: BMISS interrupts may occur after association.
		 * I found this on 5210 code but it needs testing. If this is
		 * true we should disable them before assoc and re-enable them
		 * after a successful assoc + some jiffies.
			interrupt_mask &= ~AR5K_INT_BMISS;
		 */
	}

	/*
	 * In case we didn't handle anything,
	 * print the register value.
	 */
	if (unlikely(*interrupt_mask == 0 && net_ratelimit()))
		ATH5K_PRINTF("ISR: 0x%08x IMR: 0x%08x\n", data, ah->ah_imr);

	return 0;
}

/**
 * ath5k_hw_set_imr - Set interrupt mask
 *
 * @ah: The &struct ath5k_hw
 * @new_mask: The new interrupt mask to be set
 *
 * Set the interrupt mask in hw to save interrupts. We do that by mapping
 * ath5k_int bits to hw-specific bits to remove abstraction and writing
 * Interrupt Mask Register.
 */
enum ath5k_int ath5k_hw_set_imr(struct ath5k_hw *ah, enum ath5k_int new_mask)
{
	enum ath5k_int old_mask, int_mask;

	old_mask = ah->ah_imr;

	/*
	 * Disable card interrupts to prevent any race conditions
	 * (they will be re-enabled afterwards if AR5K_INT GLOBAL
	 * is set again on the new mask).
	 */
	if (old_mask & AR5K_INT_GLOBAL) {
		ath5k_hw_reg_write(ah, AR5K_IER_DISABLE, AR5K_IER);
		ath5k_hw_reg_read(ah, AR5K_IER);
	}

	/*
	 * Add additional, chipset-dependent interrupt mask flags
	 * and write them to the IMR (interrupt mask register).
	 */
	int_mask = new_mask & AR5K_INT_COMMON;

	if (ah->ah_version != AR5K_AR5210) {
		/* Preserve per queue TXURN interrupt mask */
		u32 simr2 = ath5k_hw_reg_read(ah, AR5K_SIMR2)
				& AR5K_SIMR2_QCU_TXURN;

		if (new_mask & AR5K_INT_FATAL) {
			int_mask |= AR5K_IMR_HIUERR;
			simr2 |= (AR5K_SIMR2_MCABT | AR5K_SIMR2_SSERR
				| AR5K_SIMR2_DPERR);
		}

		/*Beacon Not Ready*/
		if (new_mask & AR5K_INT_BNR)
			int_mask |= AR5K_INT_BNR;

		if (new_mask & AR5K_INT_TIM)
			int_mask |= AR5K_IMR_TIM;

		if (new_mask & AR5K_INT_TIM)
			simr2 |= AR5K_SISR2_TIM;
		if (new_mask & AR5K_INT_DTIM)
			simr2 |= AR5K_SISR2_DTIM;
		if (new_mask & AR5K_INT_DTIM_SYNC)
			simr2 |= AR5K_SISR2_DTIM_SYNC;
		if (new_mask & AR5K_INT_BCN_TIMEOUT)
			simr2 |= AR5K_SISR2_BCN_TIMEOUT;
		if (new_mask & AR5K_INT_CAB_TIMEOUT)
			simr2 |= AR5K_SISR2_CAB_TIMEOUT;

		if (new_mask & AR5K_INT_RX_DOPPLER)
			int_mask |= AR5K_IMR_RXDOPPLER;

		/* Note: Per queue interrupt masks
		 * are set via reset_tx_queue (qcu.c) */
		ath5k_hw_reg_write(ah, int_mask, AR5K_PIMR);
		ath5k_hw_reg_write(ah, simr2, AR5K_SIMR2);

	} else {
		if (new_mask & AR5K_INT_FATAL)
			int_mask |= (AR5K_IMR_SSERR | AR5K_IMR_MCABT
				| AR5K_IMR_HIUERR | AR5K_IMR_DPERR);

		ath5k_hw_reg_write(ah, int_mask, AR5K_IMR);
	}

	/* If RXNOFRM interrupt is masked disable it
	 * by setting AR5K_RXNOFRM to zero */
	if (!(new_mask & AR5K_INT_RXNOFRM))
		ath5k_hw_reg_write(ah, 0, AR5K_RXNOFRM);

	/* Store new interrupt mask */
	ah->ah_imr = new_mask;

	/* ..re-enable interrupts if AR5K_INT_GLOBAL is set */
	if (new_mask & AR5K_INT_GLOBAL) {
		ath5k_hw_reg_write(ah, AR5K_IER_ENABLE, AR5K_IER);
		ath5k_hw_reg_read(ah, AR5K_IER);
	}

	return old_mask;
}


/********************\
 Init/Stop functions
\********************/

/**
 * ath5k_hw_dma_init - Initialize DMA unit
 *
 * @ah: The &struct ath5k_hw
 *
 * Set DMA size and pre-enable interrupts
 * (driver handles tx/rx buffer setup and
 * dma start/stop)
 *
 * XXX: Save/restore RXDP/TXDP registers ?
 */
void ath5k_hw_dma_init(struct ath5k_hw *ah)
{
	/*
	 * Set Rx/Tx DMA Configuration
	 *
	 * Set standard DMA size (128). Note that
	 * a DMA size of 512 causes rx overruns and tx errors
	 * on pci-e cards (tested on 5424 but since rx overruns
	 * also occur on 5416/5418 with madwifi we set 128
	 * for all PCI-E cards to be safe).
	 *
	 * XXX: need to check 5210 for this
	 * TODO: Check out tx triger level, it's always 64 on dumps but I
	 * guess we can tweak it and see how it goes ;-)
	 */
	if (ah->ah_version != AR5K_AR5210) {
		AR5K_REG_WRITE_BITS(ah, AR5K_TXCFG,
			AR5K_TXCFG_SDMAMR, AR5K_DMASIZE_128B);
		AR5K_REG_WRITE_BITS(ah, AR5K_RXCFG,
			AR5K_RXCFG_SDMAMW, AR5K_DMASIZE_128B);
	}

	/* Pre-enable interrupts on 5211/5212*/
	if (ah->ah_version != AR5K_AR5210)
		ath5k_hw_set_imr(ah, ah->ah_imr);

}

/**
 * ath5k_hw_dma_stop - stop DMA unit
 *
 * @ah: The &struct ath5k_hw
 *
 * Stop tx/rx DMA and interrupts. Returns
 * -EBUSY if tx or rx dma failed to stop.
 *
 * XXX: Sometimes DMA unit hangs and we have
 * stuck frames on tx queues, only a reset
 * can fix that.
 */
int ath5k_hw_dma_stop(struct ath5k_hw *ah)
{
	int i, qmax, err;
	err = 0;

	/* Disable interrupts */
	ath5k_hw_set_imr(ah, 0);

	/* Stop rx dma */
	err = ath5k_hw_stop_rx_dma(ah);
	if (err)
		return err;

	/* Clear any pending interrupts
	 * and disable tx dma */
	if (ah->ah_version != AR5K_AR5210) {
		ath5k_hw_reg_write(ah, 0xffffffff, AR5K_PISR);
		qmax = AR5K_NUM_TX_QUEUES;
	} else {
		/* PISR/SISR Not available on 5210 */
		ath5k_hw_reg_read(ah, AR5K_ISR);
		qmax = AR5K_NUM_TX_QUEUES_NOQCU;
	}

	for (i = 0; i < qmax; i++) {
		err = ath5k_hw_stop_tx_dma(ah, i);
		/* -EINVAL -> queue inactive */
		if (err && err != -EINVAL)
			return err;
	}

	return 0;
}
