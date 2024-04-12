// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008-2010
 *
 * - Kurt Van Dijck, EIA Electronics
 */

#include <linux/firmware.h>
#include <linux/sched/signal.h>
#include <asm/div64.h>
#include <asm/io.h>

#include "softing.h"

/*
 * low level DPRAM command.
 * Make sure that card->dpram[DPRAM_FCT_HOST] is preset
 */
static int _softing_fct_cmd(struct softing *card, int16_t cmd, uint16_t vector,
		const char *msg)
{
	int ret;
	unsigned long stamp;

	iowrite16(cmd, &card->dpram[DPRAM_FCT_PARAM]);
	iowrite8(vector >> 8, &card->dpram[DPRAM_FCT_HOST + 1]);
	iowrite8(vector, &card->dpram[DPRAM_FCT_HOST]);
	/* be sure to flush this to the card */
	wmb();
	stamp = jiffies + 1 * HZ;
	/* wait for card */
	do {
		/* DPRAM_FCT_HOST is _not_ aligned */
		ret = ioread8(&card->dpram[DPRAM_FCT_HOST]) +
			(ioread8(&card->dpram[DPRAM_FCT_HOST + 1]) << 8);
		/* don't have any cached variables */
		rmb();
		if (ret == RES_OK)
			/* read return-value now */
			return ioread16(&card->dpram[DPRAM_FCT_RESULT]);

		if ((ret != vector) || time_after(jiffies, stamp))
			break;
		/* process context => relax */
		usleep_range(500, 10000);
	} while (1);

	ret = (ret == RES_NONE) ? -ETIMEDOUT : -ECANCELED;
	dev_alert(&card->pdev->dev, "firmware %s failed (%i)\n", msg, ret);
	return ret;
}

static int softing_fct_cmd(struct softing *card, int16_t cmd, const char *msg)
{
	int ret;

	ret = _softing_fct_cmd(card, cmd, 0, msg);
	if (ret > 0) {
		dev_alert(&card->pdev->dev, "%s returned %u\n", msg, ret);
		ret = -EIO;
	}
	return ret;
}

int softing_bootloader_command(struct softing *card, int16_t cmd,
		const char *msg)
{
	int ret;
	unsigned long stamp;

	iowrite16(RES_NONE, &card->dpram[DPRAM_RECEIPT]);
	iowrite16(cmd, &card->dpram[DPRAM_COMMAND]);
	/* be sure to flush this to the card */
	wmb();
	stamp = jiffies + 3 * HZ;
	/* wait for card */
	do {
		ret = ioread16(&card->dpram[DPRAM_RECEIPT]);
		/* don't have any cached variables */
		rmb();
		if (ret == RES_OK)
			return 0;
		if (time_after(jiffies, stamp))
			break;
		/* process context => relax */
		usleep_range(500, 10000);
	} while (!signal_pending(current));

	ret = (ret == RES_NONE) ? -ETIMEDOUT : -ECANCELED;
	dev_alert(&card->pdev->dev, "bootloader %s failed (%i)\n", msg, ret);
	return ret;
}

static int fw_parse(const uint8_t **pmem, uint16_t *ptype, uint32_t *paddr,
		uint16_t *plen, const uint8_t **pdat)
{
	uint16_t checksum[2];
	const uint8_t *mem;
	const uint8_t *end;

	/*
	 * firmware records are a binary, unaligned stream composed of:
	 * uint16_t type;
	 * uint32_t addr;
	 * uint16_t len;
	 * uint8_t dat[len];
	 * uint16_t checksum;
	 * all values in little endian.
	 * We could define a struct for this, with __attribute__((packed)),
	 * but would that solve the alignment in _all_ cases (cfr. the
	 * struct itself may be an odd address)?
	 *
	 * I chose to use leXX_to_cpup() since this solves both
	 * endianness & alignment.
	 */
	mem = *pmem;
	*ptype = le16_to_cpup((void *)&mem[0]);
	*paddr = le32_to_cpup((void *)&mem[2]);
	*plen = le16_to_cpup((void *)&mem[6]);
	*pdat = &mem[8];
	/* verify checksum */
	end = &mem[8 + *plen];
	checksum[0] = le16_to_cpup((void *)end);
	for (checksum[1] = 0; mem < end; ++mem)
		checksum[1] += *mem;
	if (checksum[0] != checksum[1])
		return -EINVAL;
	/* increment */
	*pmem += 10 + *plen;
	return 0;
}

int softing_load_fw(const char *file, struct softing *card,
		__iomem uint8_t *dpram, unsigned int size, int offset)
{
	const struct firmware *fw;
	int ret;
	const uint8_t *mem, *end, *dat;
	uint16_t type, len;
	uint32_t addr;
	uint8_t *buf = NULL, *new_buf;
	int buflen = 0;
	int8_t type_end = 0;

	ret = request_firmware(&fw, file, &card->pdev->dev);
	if (ret < 0)
		return ret;
	dev_dbg(&card->pdev->dev, "%s, firmware(%s) got %u bytes"
		", offset %c0x%04x\n",
		card->pdat->name, file, (unsigned int)fw->size,
		(offset >= 0) ? '+' : '-', (unsigned int)abs(offset));
	/* parse the firmware */
	mem = fw->data;
	end = &mem[fw->size];
	/* look for header record */
	ret = fw_parse(&mem, &type, &addr, &len, &dat);
	if (ret < 0)
		goto failed;
	if (type != 0xffff)
		goto failed;
	if (strncmp("Structured Binary Format, Softing GmbH" , dat, len)) {
		ret = -EINVAL;
		goto failed;
	}
	/* ok, we had a header */
	while (mem < end) {
		ret = fw_parse(&mem, &type, &addr, &len, &dat);
		if (ret < 0)
			goto failed;
		if (type == 3) {
			/* start address, not used here */
			continue;
		} else if (type == 1) {
			/* eof */
			type_end = 1;
			break;
		} else if (type != 0) {
			ret = -EINVAL;
			goto failed;
		}

		if ((addr + len + offset) > size)
			goto failed;
		memcpy_toio(&dpram[addr + offset], dat, len);
		/* be sure to flush caches from IO space */
		mb();
		if (len > buflen) {
			/* align buflen */
			buflen = (len + (1024-1)) & ~(1024-1);
			new_buf = krealloc(buf, buflen, GFP_KERNEL);
			if (!new_buf) {
				ret = -ENOMEM;
				goto failed;
			}
			buf = new_buf;
		}
		/* verify record data */
		memcpy_fromio(buf, &dpram[addr + offset], len);
		if (memcmp(buf, dat, len)) {
			/* is not ok */
			dev_alert(&card->pdev->dev, "DPRAM readback failed\n");
			ret = -EIO;
			goto failed;
		}
	}
	if (!type_end)
		/* no end record seen */
		goto failed;
	ret = 0;
failed:
	kfree(buf);
	release_firmware(fw);
	if (ret < 0)
		dev_info(&card->pdev->dev, "firmware %s failed\n", file);
	return ret;
}

int softing_load_app_fw(const char *file, struct softing *card)
{
	const struct firmware *fw;
	const uint8_t *mem, *end, *dat;
	int ret, j;
	uint16_t type, len;
	uint32_t addr, start_addr = 0;
	unsigned int sum, rx_sum;
	int8_t type_end = 0, type_entrypoint = 0;

	ret = request_firmware(&fw, file, &card->pdev->dev);
	if (ret) {
		dev_alert(&card->pdev->dev, "request_firmware(%s) got %i\n",
			file, ret);
		return ret;
	}
	dev_dbg(&card->pdev->dev, "firmware(%s) got %lu bytes\n",
		file, (unsigned long)fw->size);
	/* parse the firmware */
	mem = fw->data;
	end = &mem[fw->size];
	/* look for header record */
	ret = fw_parse(&mem, &type, &addr, &len, &dat);
	if (ret)
		goto failed;
	ret = -EINVAL;
	if (type != 0xffff) {
		dev_alert(&card->pdev->dev, "firmware starts with type 0x%x\n",
			type);
		goto failed;
	}
	if (strncmp("Structured Binary Format, Softing GmbH", dat, len)) {
		dev_alert(&card->pdev->dev, "firmware string '%.*s' fault\n",
				len, dat);
		goto failed;
	}
	/* ok, we had a header */
	while (mem < end) {
		ret = fw_parse(&mem, &type, &addr, &len, &dat);
		if (ret)
			goto failed;

		if (type == 3) {
			/* start address */
			start_addr = addr;
			type_entrypoint = 1;
			continue;
		} else if (type == 1) {
			/* eof */
			type_end = 1;
			break;
		} else if (type != 0) {
			dev_alert(&card->pdev->dev,
					"unknown record type 0x%04x\n", type);
			ret = -EINVAL;
			goto failed;
		}

		/* regular data */
		for (sum = 0, j = 0; j < len; ++j)
			sum += dat[j];
		/* work in 16bit (target) */
		sum &= 0xffff;

		memcpy_toio(&card->dpram[card->pdat->app.offs], dat, len);
		iowrite32(card->pdat->app.offs + card->pdat->app.addr,
				&card->dpram[DPRAM_COMMAND + 2]);
		iowrite32(addr, &card->dpram[DPRAM_COMMAND + 6]);
		iowrite16(len, &card->dpram[DPRAM_COMMAND + 10]);
		iowrite8(1, &card->dpram[DPRAM_COMMAND + 12]);
		ret = softing_bootloader_command(card, 1, "loading app.");
		if (ret < 0)
			goto failed;
		/* verify checksum */
		rx_sum = ioread16(&card->dpram[DPRAM_RECEIPT + 2]);
		if (rx_sum != sum) {
			dev_alert(&card->pdev->dev, "SRAM seems to be damaged"
				", wanted 0x%04x, got 0x%04x\n", sum, rx_sum);
			ret = -EIO;
			goto failed;
		}
	}
	if (!type_end || !type_entrypoint)
		goto failed;
	/* start application in card */
	iowrite32(start_addr, &card->dpram[DPRAM_COMMAND + 2]);
	iowrite8(1, &card->dpram[DPRAM_COMMAND + 6]);
	ret = softing_bootloader_command(card, 3, "start app.");
	if (ret < 0)
		goto failed;
	ret = 0;
failed:
	release_firmware(fw);
	if (ret < 0)
		dev_info(&card->pdev->dev, "firmware %s failed\n", file);
	return ret;
}

static int softing_reset_chip(struct softing *card)
{
	int ret;

	do {
		/* reset chip */
		iowrite8(0, &card->dpram[DPRAM_RESET_RX_FIFO]);
		iowrite8(0, &card->dpram[DPRAM_RESET_RX_FIFO+1]);
		iowrite8(1, &card->dpram[DPRAM_RESET]);
		iowrite8(0, &card->dpram[DPRAM_RESET+1]);

		ret = softing_fct_cmd(card, 0, "reset_can");
		if (!ret)
			break;
		if (signal_pending(current))
			/* don't wait any longer */
			break;
	} while (1);
	card->tx.pending = 0;
	return ret;
}

int softing_chip_poweron(struct softing *card)
{
	int ret;
	/* sync */
	ret = _softing_fct_cmd(card, 99, 0x55, "sync-a");
	if (ret < 0)
		goto failed;

	ret = _softing_fct_cmd(card, 99, 0xaa, "sync-b");
	if (ret < 0)
		goto failed;

	ret = softing_reset_chip(card);
	if (ret < 0)
		goto failed;
	/* get_serial */
	ret = softing_fct_cmd(card, 43, "get_serial_number");
	if (ret < 0)
		goto failed;
	card->id.serial = ioread32(&card->dpram[DPRAM_FCT_PARAM]);
	/* get_version */
	ret = softing_fct_cmd(card, 12, "get_version");
	if (ret < 0)
		goto failed;
	card->id.fw_version = ioread16(&card->dpram[DPRAM_FCT_PARAM + 2]);
	card->id.hw_version = ioread16(&card->dpram[DPRAM_FCT_PARAM + 4]);
	card->id.license = ioread16(&card->dpram[DPRAM_FCT_PARAM + 6]);
	card->id.chip[0] = ioread16(&card->dpram[DPRAM_FCT_PARAM + 8]);
	card->id.chip[1] = ioread16(&card->dpram[DPRAM_FCT_PARAM + 10]);
	return 0;
failed:
	return ret;
}

static void softing_initialize_timestamp(struct softing *card)
{
	uint64_t ovf;

	card->ts_ref = ktime_get();

	/* 16MHz is the reference */
	ovf = 0x100000000ULL * 16;
	do_div(ovf, card->pdat->freq ?: 16);

	card->ts_overflow = ktime_add_us(0, ovf);
}

ktime_t softing_raw2ktime(struct softing *card, u32 raw)
{
	uint64_t rawl;
	ktime_t now, real_offset;
	ktime_t target;
	ktime_t tmp;

	now = ktime_get();
	real_offset = ktime_sub(ktime_get_real(), now);

	/* find nsec from card */
	rawl = raw * 16;
	do_div(rawl, card->pdat->freq ?: 16);
	target = ktime_add_us(card->ts_ref, rawl);
	/* test for overflows */
	tmp = ktime_add(target, card->ts_overflow);
	while (unlikely(ktime_to_ns(tmp) > ktime_to_ns(now))) {
		card->ts_ref = ktime_add(card->ts_ref, card->ts_overflow);
		target = tmp;
		tmp = ktime_add(target, card->ts_overflow);
	}
	return ktime_add(target, real_offset);
}

static inline int softing_error_reporting(struct net_device *netdev)
{
	struct softing_priv *priv = netdev_priv(netdev);

	return (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		? 1 : 0;
}

int softing_startstop(struct net_device *dev, int up)
{
	int ret;
	struct softing *card;
	struct softing_priv *priv;
	struct net_device *netdev;
	int bus_bitmask_start;
	int j, error_reporting;
	struct can_frame msg;
	const struct can_bittiming *bt;

	priv = netdev_priv(dev);
	card = priv->card;

	if (!card->fw.up)
		return -EIO;

	ret = mutex_lock_interruptible(&card->fw.lock);
	if (ret)
		return ret;

	bus_bitmask_start = 0;
	if (up)
		/* prepare to start this bus as well */
		bus_bitmask_start |= (1 << priv->index);
	/* bring netdevs down */
	for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
		netdev = card->net[j];
		if (!netdev)
			continue;
		priv = netdev_priv(netdev);

		if (dev != netdev)
			netif_stop_queue(netdev);

		if (netif_running(netdev)) {
			if (dev != netdev)
				bus_bitmask_start |= (1 << j);
			priv->tx.pending = 0;
			priv->tx.echo_put = 0;
			priv->tx.echo_get = 0;
			/*
			 * this bus' may just have called open_candev()
			 * which is rather stupid to call close_candev()
			 * already
			 * but we may come here from busoff recovery too
			 * in which case the echo_skb _needs_ flushing too.
			 * just be sure to call open_candev() again
			 */
			close_candev(netdev);
		}
		priv->can.state = CAN_STATE_STOPPED;
	}
	card->tx.pending = 0;

	softing_enable_irq(card, 0);
	ret = softing_reset_chip(card);
	if (ret)
		goto failed;
	if (!bus_bitmask_start)
		/* no buses to be brought up */
		goto card_done;

	if ((bus_bitmask_start & 1) && (bus_bitmask_start & 2)
			&& (softing_error_reporting(card->net[0])
				!= softing_error_reporting(card->net[1]))) {
		dev_alert(&card->pdev->dev,
				"err_reporting flag differs for buses\n");
		goto invalid;
	}
	error_reporting = 0;
	if (bus_bitmask_start & 1) {
		netdev = card->net[0];
		priv = netdev_priv(netdev);
		error_reporting += softing_error_reporting(netdev);
		/* init chip 1 */
		bt = &priv->can.bittiming;
		iowrite16(bt->brp, &card->dpram[DPRAM_FCT_PARAM + 2]);
		iowrite16(bt->sjw, &card->dpram[DPRAM_FCT_PARAM + 4]);
		iowrite16(bt->phase_seg1 + bt->prop_seg,
				&card->dpram[DPRAM_FCT_PARAM + 6]);
		iowrite16(bt->phase_seg2, &card->dpram[DPRAM_FCT_PARAM + 8]);
		iowrite16((priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES) ? 1 : 0,
				&card->dpram[DPRAM_FCT_PARAM + 10]);
		ret = softing_fct_cmd(card, 1, "initialize_chip[0]");
		if (ret < 0)
			goto failed;
		/* set mode */
		iowrite16(0, &card->dpram[DPRAM_FCT_PARAM + 2]);
		iowrite16(0, &card->dpram[DPRAM_FCT_PARAM + 4]);
		ret = softing_fct_cmd(card, 3, "set_mode[0]");
		if (ret < 0)
			goto failed;
		/* set filter */
		/* 11bit id & mask */
		iowrite16(0x0000, &card->dpram[DPRAM_FCT_PARAM + 2]);
		iowrite16(0x07ff, &card->dpram[DPRAM_FCT_PARAM + 4]);
		/* 29bit id.lo & mask.lo & id.hi & mask.hi */
		iowrite16(0x0000, &card->dpram[DPRAM_FCT_PARAM + 6]);
		iowrite16(0xffff, &card->dpram[DPRAM_FCT_PARAM + 8]);
		iowrite16(0x0000, &card->dpram[DPRAM_FCT_PARAM + 10]);
		iowrite16(0x1fff, &card->dpram[DPRAM_FCT_PARAM + 12]);
		ret = softing_fct_cmd(card, 7, "set_filter[0]");
		if (ret < 0)
			goto failed;
		/* set output control */
		iowrite16(priv->output, &card->dpram[DPRAM_FCT_PARAM + 2]);
		ret = softing_fct_cmd(card, 5, "set_output[0]");
		if (ret < 0)
			goto failed;
	}
	if (bus_bitmask_start & 2) {
		netdev = card->net[1];
		priv = netdev_priv(netdev);
		error_reporting += softing_error_reporting(netdev);
		/* init chip2 */
		bt = &priv->can.bittiming;
		iowrite16(bt->brp, &card->dpram[DPRAM_FCT_PARAM + 2]);
		iowrite16(bt->sjw, &card->dpram[DPRAM_FCT_PARAM + 4]);
		iowrite16(bt->phase_seg1 + bt->prop_seg,
				&card->dpram[DPRAM_FCT_PARAM + 6]);
		iowrite16(bt->phase_seg2, &card->dpram[DPRAM_FCT_PARAM + 8]);
		iowrite16((priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES) ? 1 : 0,
				&card->dpram[DPRAM_FCT_PARAM + 10]);
		ret = softing_fct_cmd(card, 2, "initialize_chip[1]");
		if (ret < 0)
			goto failed;
		/* set mode2 */
		iowrite16(0, &card->dpram[DPRAM_FCT_PARAM + 2]);
		iowrite16(0, &card->dpram[DPRAM_FCT_PARAM + 4]);
		ret = softing_fct_cmd(card, 4, "set_mode[1]");
		if (ret < 0)
			goto failed;
		/* set filter2 */
		/* 11bit id & mask */
		iowrite16(0x0000, &card->dpram[DPRAM_FCT_PARAM + 2]);
		iowrite16(0x07ff, &card->dpram[DPRAM_FCT_PARAM + 4]);
		/* 29bit id.lo & mask.lo & id.hi & mask.hi */
		iowrite16(0x0000, &card->dpram[DPRAM_FCT_PARAM + 6]);
		iowrite16(0xffff, &card->dpram[DPRAM_FCT_PARAM + 8]);
		iowrite16(0x0000, &card->dpram[DPRAM_FCT_PARAM + 10]);
		iowrite16(0x1fff, &card->dpram[DPRAM_FCT_PARAM + 12]);
		ret = softing_fct_cmd(card, 8, "set_filter[1]");
		if (ret < 0)
			goto failed;
		/* set output control2 */
		iowrite16(priv->output, &card->dpram[DPRAM_FCT_PARAM + 2]);
		ret = softing_fct_cmd(card, 6, "set_output[1]");
		if (ret < 0)
			goto failed;
	}

	/* enable_error_frame
	 *
	 * Error reporting is switched off at the moment since
	 * the receiving of them is not yet 100% verified
	 * This should be enabled sooner or later
	 */
	if (0 && error_reporting) {
		ret = softing_fct_cmd(card, 51, "enable_error_frame");
		if (ret < 0)
			goto failed;
	}

	/* initialize interface */
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 2]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 4]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 6]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 8]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 10]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 12]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 14]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 16]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 18]);
	iowrite16(1, &card->dpram[DPRAM_FCT_PARAM + 20]);
	ret = softing_fct_cmd(card, 17, "initialize_interface");
	if (ret < 0)
		goto failed;
	/* enable_fifo */
	ret = softing_fct_cmd(card, 36, "enable_fifo");
	if (ret < 0)
		goto failed;
	/* enable fifo tx ack */
	ret = softing_fct_cmd(card, 13, "fifo_tx_ack[0]");
	if (ret < 0)
		goto failed;
	/* enable fifo tx ack2 */
	ret = softing_fct_cmd(card, 14, "fifo_tx_ack[1]");
	if (ret < 0)
		goto failed;
	/* start_chip */
	ret = softing_fct_cmd(card, 11, "start_chip");
	if (ret < 0)
		goto failed;
	iowrite8(0, &card->dpram[DPRAM_INFO_BUSSTATE]);
	iowrite8(0, &card->dpram[DPRAM_INFO_BUSSTATE2]);
	if (card->pdat->generation < 2) {
		iowrite8(0, &card->dpram[DPRAM_V2_IRQ_TOHOST]);
		/* flush the DPRAM caches */
		wmb();
	}

	softing_initialize_timestamp(card);

	/*
	 * do socketcan notifications/status changes
	 * from here, no errors should occur, or the failed: part
	 * must be reviewed
	 */
	memset(&msg, 0, sizeof(msg));
	msg.can_id = CAN_ERR_FLAG | CAN_ERR_RESTARTED;
	msg.len = CAN_ERR_DLC;
	for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
		if (!(bus_bitmask_start & (1 << j)))
			continue;
		netdev = card->net[j];
		if (!netdev)
			continue;
		priv = netdev_priv(netdev);
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
		open_candev(netdev);
		if (dev != netdev) {
			/* notify other buses on the restart */
			softing_netdev_rx(netdev, &msg, 0);
			++priv->can.can_stats.restarts;
		}
		netif_wake_queue(netdev);
	}

	/* enable interrupts */
	ret = softing_enable_irq(card, 1);
	if (ret)
		goto failed;
card_done:
	mutex_unlock(&card->fw.lock);
	return 0;
invalid:
	ret = -EINVAL;
failed:
	softing_enable_irq(card, 0);
	softing_reset_chip(card);
	mutex_unlock(&card->fw.lock);
	/* bring all other interfaces down */
	for (j = 0; j < ARRAY_SIZE(card->net); ++j) {
		netdev = card->net[j];
		if (!netdev)
			continue;
		dev_close(netdev);
	}
	return ret;
}

int softing_default_output(struct net_device *netdev)
{
	struct softing_priv *priv = netdev_priv(netdev);
	struct softing *card = priv->card;

	switch (priv->chip) {
	case 1000:
		return (card->pdat->generation < 2) ? 0xfb : 0xfa;
	case 5:
		return 0x60;
	default:
		return 0x40;
	}
}
