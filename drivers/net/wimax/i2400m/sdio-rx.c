/*
 * Intel Wireless WiMAX Connection 2400m
 * SDIO RX handling
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <linux-wimax@intel.com>
 * Dirk Brandewie <dirk.j.brandewie@intel.com>
 *  - Initial implementation
 *
 *
 * This handles the RX path on SDIO.
 *
 * The SDIO bus driver calls the "irq" routine when data is available.
 * This is not a traditional interrupt routine since the SDIO bus
 * driver calls us from its irq thread context.  Because of this
 * sleeping in the SDIO RX IRQ routine is okay.
 *
 * From there on, we obtain the size of the data that is available,
 * allocate an skb, copy it and then pass it to the generic driver's
 * RX routine [i2400m_rx()].
 *
 * ROADMAP
 *
 * i2400ms_irq()
 *   i2400ms_rx()
 *     __i2400ms_rx_get_size()
 *     i2400m_is_boot_barker()
 *     i2400m_rx()
 *
 * i2400ms_rx_setup()
 *
 * i2400ms_rx_release()
 */
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/slab.h>
#include "i2400m-sdio.h"

#define D_SUBMODULE rx
#include "sdio-debug-levels.h"

static const __le32 i2400m_ACK_BARKER[4] = {
	__constant_cpu_to_le32(I2400M_ACK_BARKER),
	__constant_cpu_to_le32(I2400M_ACK_BARKER),
	__constant_cpu_to_le32(I2400M_ACK_BARKER),
	__constant_cpu_to_le32(I2400M_ACK_BARKER)
};


/*
 * Read and return the amount of bytes available for RX
 *
 * The RX size has to be read like this: byte reads of three
 * sequential locations; then glue'em together.
 *
 * sdio_readl() doesn't work.
 */
static ssize_t __i2400ms_rx_get_size(struct i2400ms *i2400ms)
{
	int ret, cnt, val;
	ssize_t rx_size;
	unsigned xfer_size_addr;
	struct sdio_func *func = i2400ms->func;
	struct device *dev = &i2400ms->func->dev;

	d_fnstart(7, dev, "(i2400ms %p)\n", i2400ms);
	xfer_size_addr = I2400MS_INTR_GET_SIZE_ADDR;
	rx_size = 0;
	for (cnt = 0; cnt < 3; cnt++) {
		val = sdio_readb(func, xfer_size_addr + cnt, &ret);
		if (ret < 0) {
			dev_err(dev, "RX: Can't read byte %d of RX size from "
				"0x%08x: %d\n", cnt, xfer_size_addr + cnt, ret);
			rx_size = ret;
			goto error_read;
		}
		rx_size = rx_size << 8 | (val & 0xff);
	}
	d_printf(6, dev, "RX: rx_size is %ld\n", (long) rx_size);
error_read:
	d_fnend(7, dev, "(i2400ms %p) = %ld\n", i2400ms, (long) rx_size);
	return rx_size;
}


/*
 * Read data from the device (when in normal)
 *
 * Allocate an SKB of the right size, read the data in and then
 * deliver it to the generic layer.
 *
 * We also check for a reboot barker. That means the device died and
 * we have to reboot it.
 */
static
void i2400ms_rx(struct i2400ms *i2400ms)
{
	int ret;
	struct sdio_func *func = i2400ms->func;
	struct device *dev = &func->dev;
	struct i2400m *i2400m = &i2400ms->i2400m;
	struct sk_buff *skb;
	ssize_t rx_size;

	d_fnstart(7, dev, "(i2400ms %p)\n", i2400ms);
	rx_size = __i2400ms_rx_get_size(i2400ms);
	if (rx_size < 0) {
		ret = rx_size;
		goto error_get_size;
	}
	/*
	 * Hardware quirk: make sure to clear the INTR status register
	 * AFTER getting the data transfer size.
	 */
	sdio_writeb(func, 1, I2400MS_INTR_CLEAR_ADDR, &ret);

	ret = -ENOMEM;
	skb = alloc_skb(rx_size, GFP_ATOMIC);
	if (NULL == skb) {
		dev_err(dev, "RX: unable to alloc skb\n");
		goto error_alloc_skb;
	}
	ret = sdio_memcpy_fromio(func, skb->data,
				 I2400MS_DATA_ADDR, rx_size);
	if (ret < 0) {
		dev_err(dev, "RX: SDIO data read failed: %d\n", ret);
		goto error_memcpy_fromio;
	}

	rmb();	/* make sure we get boot_mode from dev_reset_handle */
	if (unlikely(i2400m->boot_mode == 1)) {
		spin_lock(&i2400m->rx_lock);
		i2400ms->bm_ack_size = rx_size;
		spin_unlock(&i2400m->rx_lock);
		memcpy(i2400m->bm_ack_buf, skb->data, rx_size);
		wake_up(&i2400ms->bm_wfa_wq);
		d_printf(5, dev, "RX: SDIO boot mode message\n");
		kfree_skb(skb);
		goto out;
	}
	ret = -EIO;
	if (unlikely(rx_size < sizeof(__le32))) {
		dev_err(dev, "HW BUG? only %zu bytes received\n", rx_size);
		goto error_bad_size;
	}
	if (likely(i2400m_is_d2h_barker(skb->data))) {
		skb_put(skb, rx_size);
		i2400m_rx(i2400m, skb);
	} else if (unlikely(i2400m_is_boot_barker(i2400m,
						  skb->data, rx_size))) {
		ret = i2400m_dev_reset_handle(i2400m, "device rebooted");
		dev_err(dev, "RX: SDIO reboot barker\n");
		kfree_skb(skb);
	} else {
		i2400m_unknown_barker(i2400m, skb->data, rx_size);
		kfree_skb(skb);
	}
out:
	d_fnend(7, dev, "(i2400ms %p) = void\n", i2400ms);
	return;

error_memcpy_fromio:
	kfree_skb(skb);
error_alloc_skb:
error_get_size:
error_bad_size:
	d_fnend(7, dev, "(i2400ms %p) = %d\n", i2400ms, ret);
}


/*
 * Process an interrupt from the SDIO card
 *
 * FIXME: need to process other events that are not just ready-to-read
 *
 * Checks there is data ready and then proceeds to read it.
 */
static
void i2400ms_irq(struct sdio_func *func)
{
	int ret;
	struct i2400ms *i2400ms = sdio_get_drvdata(func);
	struct device *dev = &func->dev;
	int val;

	d_fnstart(6, dev, "(i2400ms %p)\n", i2400ms);
	val = sdio_readb(func, I2400MS_INTR_STATUS_ADDR, &ret);
	if (ret < 0) {
		dev_err(dev, "RX: Can't read interrupt status: %d\n", ret);
		goto error_no_irq;
	}
	if (!val) {
		dev_err(dev, "RX: BUG? got IRQ but no interrupt ready?\n");
		goto error_no_irq;
	}
	i2400ms_rx(i2400ms);
error_no_irq:
	d_fnend(6, dev, "(i2400ms %p) = void\n", i2400ms);
}


/*
 * Setup SDIO RX
 *
 * Hooks up the IRQ handler and then enables IRQs.
 */
int i2400ms_rx_setup(struct i2400ms *i2400ms)
{
	int result;
	struct sdio_func *func = i2400ms->func;
	struct device *dev = &func->dev;
	struct i2400m *i2400m = &i2400ms->i2400m;

	d_fnstart(5, dev, "(i2400ms %p)\n", i2400ms);

	init_waitqueue_head(&i2400ms->bm_wfa_wq);
	spin_lock(&i2400m->rx_lock);
	i2400ms->bm_wait_result = -EINPROGRESS;
	/*
	 * Before we are about to enable the RX interrupt, make sure
	 * bm_ack_size is cleared to -EINPROGRESS which indicates
	 * no RX interrupt happened yet or the previous interrupt
	 * has been handled, we are ready to take the new interrupt
	 */
	i2400ms->bm_ack_size = -EINPROGRESS;
	spin_unlock(&i2400m->rx_lock);

	sdio_claim_host(func);
	result = sdio_claim_irq(func, i2400ms_irq);
	if (result < 0) {
		dev_err(dev, "Cannot claim IRQ: %d\n", result);
		goto error_irq_claim;
	}
	result = 0;
	sdio_writeb(func, 1, I2400MS_INTR_ENABLE_ADDR, &result);
	if (result < 0) {
		sdio_release_irq(func);
		dev_err(dev, "Failed to enable interrupts %d\n", result);
	}
error_irq_claim:
	sdio_release_host(func);
	d_fnend(5, dev, "(i2400ms %p) = %d\n", i2400ms, result);
	return result;
}


/*
 * Tear down SDIO RX
 *
 * Disables IRQs in the device and removes the IRQ handler.
 */
void i2400ms_rx_release(struct i2400ms *i2400ms)
{
	int result;
	struct sdio_func *func = i2400ms->func;
	struct device *dev = &func->dev;
	struct i2400m *i2400m = &i2400ms->i2400m;

	d_fnstart(5, dev, "(i2400ms %p)\n", i2400ms);
	spin_lock(&i2400m->rx_lock);
	i2400ms->bm_ack_size = -EINTR;
	spin_unlock(&i2400m->rx_lock);
	wake_up_all(&i2400ms->bm_wfa_wq);
	sdio_claim_host(func);
	sdio_writeb(func, 0, I2400MS_INTR_ENABLE_ADDR, &result);
	sdio_release_irq(func);
	sdio_release_host(func);
	d_fnend(5, dev, "(i2400ms %p) = %d\n", i2400ms, result);
}
