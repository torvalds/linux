// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) IBM Corporation 2017
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERGCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fsi.h>
#include <linux/fsi-sbefifo.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include <uapi/linux/fsi.h>

/*
 * The SBEFIFO is a pipe-like FSI device for communicating with
 * the self boot engine on POWER processors.
 */

#define DEVICE_NAME		"sbefifo"
#define FSI_ENGID_SBE		0x22

/*
 * Register layout
 */

/* Register banks */
#define SBEFIFO_UP		0x00		/* FSI -> Host */
#define SBEFIFO_DOWN		0x40		/* Host -> FSI */

/* Per-bank registers */
#define SBEFIFO_FIFO		0x00		/* The FIFO itself */
#define SBEFIFO_STS		0x04		/* Status register */
#define   SBEFIFO_STS_PARITY_ERR	0x20000000
#define   SBEFIFO_STS_RESET_REQ		0x02000000
#define   SBEFIFO_STS_GOT_EOT		0x00800000
#define   SBEFIFO_STS_MAX_XFER_LIMIT	0x00400000
#define   SBEFIFO_STS_FULL		0x00200000
#define   SBEFIFO_STS_EMPTY		0x00100000
#define   SBEFIFO_STS_ECNT_MASK		0x000f0000
#define   SBEFIFO_STS_ECNT_SHIFT	16
#define   SBEFIFO_STS_VALID_MASK	0x0000ff00
#define   SBEFIFO_STS_VALID_SHIFT	8
#define   SBEFIFO_STS_EOT_MASK		0x000000ff
#define   SBEFIFO_STS_EOT_SHIFT		0
#define SBEFIFO_EOT_RAISE	0x08		/* (Up only) Set End Of Transfer */
#define SBEFIFO_REQ_RESET	0x0C		/* (Up only) Reset Request */
#define SBEFIFO_PERFORM_RESET	0x10		/* (Down only) Perform Reset */
#define SBEFIFO_EOT_ACK		0x14		/* (Down only) Acknowledge EOT */
#define SBEFIFO_DOWN_MAX	0x18		/* (Down only) Max transfer */

/* CFAM GP Mailbox SelfBoot Message register */
#define CFAM_GP_MBOX_SBM_ADDR	0x2824	/* Converted 0x2809 */

#define CFAM_SBM_SBE_BOOTED		0x80000000
#define CFAM_SBM_SBE_ASYNC_FFDC		0x40000000
#define CFAM_SBM_SBE_STATE_MASK		0x00f00000
#define CFAM_SBM_SBE_STATE_SHIFT	20

enum sbe_state
{
	SBE_STATE_UNKNOWN = 0x0, // Unkown, initial state
	SBE_STATE_IPLING  = 0x1, // IPL'ing - autonomous mode (transient)
	SBE_STATE_ISTEP   = 0x2, // ISTEP - Running IPL by steps (transient)
	SBE_STATE_MPIPL   = 0x3, // MPIPL
	SBE_STATE_RUNTIME = 0x4, // SBE Runtime
	SBE_STATE_DMT     = 0x5, // Dead Man Timer State (transient)
	SBE_STATE_DUMP    = 0x6, // Dumping
	SBE_STATE_FAILURE = 0x7, // Internal SBE failure
	SBE_STATE_QUIESCE = 0x8, // Final state - needs SBE reset to get out
};

/* FIFO depth */
#define SBEFIFO_FIFO_DEPTH		8

/* Helpers */
#define sbefifo_empty(sts)	((sts) & SBEFIFO_STS_EMPTY)
#define sbefifo_full(sts)	((sts) & SBEFIFO_STS_FULL)
#define sbefifo_parity_err(sts)	((sts) & SBEFIFO_STS_PARITY_ERR)
#define sbefifo_populated(sts)	(((sts) & SBEFIFO_STS_ECNT_MASK) >> SBEFIFO_STS_ECNT_SHIFT)
#define sbefifo_vacant(sts)	(SBEFIFO_FIFO_DEPTH - sbefifo_populated(sts))
#define sbefifo_eot_set(sts)	(((sts) & SBEFIFO_STS_EOT_MASK) >> SBEFIFO_STS_EOT_SHIFT)

/* Reset request timeout in ms */
#define SBEFIFO_RESET_TIMEOUT		10000

/* Timeouts for commands in ms */
#define SBEFIFO_TIMEOUT_START_CMD	10000
#define SBEFIFO_TIMEOUT_IN_CMD		1000
#define SBEFIFO_TIMEOUT_START_RSP	10000
#define SBEFIFO_TIMEOUT_IN_RSP		1000

/* Other constants */
#define SBEFIFO_MAX_USER_CMD_LEN	(0x100000 + PAGE_SIZE)
#define SBEFIFO_RESET_MAGIC		0x52534554 /* "RSET" */

struct sbefifo {
	uint32_t		magic;
#define SBEFIFO_MAGIC		0x53424546 /* "SBEF" */
	struct fsi_device	*fsi_dev;
	struct device		dev;
	struct cdev		cdev;
	struct mutex		lock;
	bool			broken;
	bool			dead;
	bool			async_ffdc;
	bool			timed_out;
	u32			timeout_start_rsp_ms;
};

struct sbefifo_user {
	struct sbefifo		*sbefifo;
	struct mutex		file_lock;
	void			*cmd_page;
	void			*pending_cmd;
	size_t			pending_len;
	u32			read_timeout_ms;
};

static DEFINE_MUTEX(sbefifo_ffdc_mutex);

static ssize_t timeout_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sbefifo *sbefifo = container_of(dev, struct sbefifo, dev);

	return sysfs_emit(buf, "%d\n", sbefifo->timed_out ? 1 : 0);
}
static DEVICE_ATTR_RO(timeout);

static void __sbefifo_dump_ffdc(struct device *dev, const __be32 *ffdc,
				size_t ffdc_sz, bool internal)
{
	int pack = 0;
#define FFDC_LSIZE	60
	static char ffdc_line[FFDC_LSIZE];
	char *p = ffdc_line;

	while (ffdc_sz) {
		u32 w0, w1, w2, i;
		if (ffdc_sz < 3) {
			dev_err(dev, "SBE invalid FFDC package size %zd\n", ffdc_sz);
			return;
		}
		w0 = be32_to_cpu(*(ffdc++));
		w1 = be32_to_cpu(*(ffdc++));
		w2 = be32_to_cpu(*(ffdc++));
		ffdc_sz -= 3;
		if ((w0 >> 16) != 0xFFDC) {
			dev_err(dev, "SBE invalid FFDC package signature %08x %08x %08x\n",
				w0, w1, w2);
			break;
		}
		w0 &= 0xffff;
		if (w0 > ffdc_sz) {
			dev_err(dev, "SBE FFDC package len %d words but only %zd remaining\n",
				w0, ffdc_sz);
			w0 = ffdc_sz;
			break;
		}
		if (internal) {
			dev_warn(dev, "+---- SBE FFDC package %d for async err -----+\n",
				 pack++);
		} else {
			dev_warn(dev, "+---- SBE FFDC package %d for cmd %02x:%02x -----+\n",
				 pack++, (w1 >> 8) & 0xff, w1 & 0xff);
		}
		dev_warn(dev, "| Response code: %08x                   |\n", w2);
		dev_warn(dev, "|-------------------------------------------|\n");
		for (i = 0; i < w0; i++) {
			if ((i & 3) == 0) {
				p = ffdc_line;
				p += sprintf(p, "| %04x:", i << 4);
			}
			p += sprintf(p, " %08x", be32_to_cpu(*(ffdc++)));
			ffdc_sz--;
			if ((i & 3) == 3 || i == (w0 - 1)) {
				while ((i & 3) < 3) {
					p += sprintf(p, "         ");
					i++;
				}
				dev_warn(dev, "%s |\n", ffdc_line);
			}
		}
		dev_warn(dev, "+-------------------------------------------+\n");
	}
}

static void sbefifo_dump_ffdc(struct device *dev, const __be32 *ffdc,
			      size_t ffdc_sz, bool internal)
{
	mutex_lock(&sbefifo_ffdc_mutex);
	__sbefifo_dump_ffdc(dev, ffdc, ffdc_sz, internal);
	mutex_unlock(&sbefifo_ffdc_mutex);
}

int sbefifo_parse_status(struct device *dev, u16 cmd, __be32 *response,
			 size_t resp_len, size_t *data_len)
{
	u32 dh, s0, s1;
	size_t ffdc_sz;

	if (resp_len < 3) {
		pr_debug("sbefifo: cmd %04x, response too small: %zd\n",
			 cmd, resp_len);
		return -ENXIO;
	}
	dh = be32_to_cpu(response[resp_len - 1]);
	if (dh > resp_len || dh < 3) {
		dev_err(dev, "SBE cmd %02x:%02x status offset out of range: %d/%zd\n",
			cmd >> 8, cmd & 0xff, dh, resp_len);
		return -ENXIO;
	}
	s0 = be32_to_cpu(response[resp_len - dh]);
	s1 = be32_to_cpu(response[resp_len - dh + 1]);
	if (((s0 >> 16) != 0xC0DE) || ((s0 & 0xffff) != cmd)) {
		dev_err(dev, "SBE cmd %02x:%02x, status signature invalid: 0x%08x 0x%08x\n",
			cmd >> 8, cmd & 0xff, s0, s1);
		return -ENXIO;
	}
	if (s1 != 0) {
		ffdc_sz = dh - 3;
		dev_warn(dev, "SBE error cmd %02x:%02x status=%04x:%04x\n",
			 cmd >> 8, cmd & 0xff, s1 >> 16, s1 & 0xffff);
		if (ffdc_sz)
			sbefifo_dump_ffdc(dev, &response[resp_len - dh + 2],
					  ffdc_sz, false);
	}
	if (data_len)
		*data_len = resp_len - dh;

	/*
	 * Primary status don't have the top bit set, so can't be confused with
	 * Linux negative error codes, so return the status word whole.
	 */
	return s1;
}
EXPORT_SYMBOL_GPL(sbefifo_parse_status);

static int sbefifo_regr(struct sbefifo *sbefifo, int reg, u32 *word)
{
	__be32 raw_word;
	int rc;

	rc = fsi_device_read(sbefifo->fsi_dev, reg, &raw_word,
			     sizeof(raw_word));
	if (rc)
		return rc;

	*word = be32_to_cpu(raw_word);

	return 0;
}

static int sbefifo_regw(struct sbefifo *sbefifo, int reg, u32 word)
{
	__be32 raw_word = cpu_to_be32(word);

	return fsi_device_write(sbefifo->fsi_dev, reg, &raw_word,
				sizeof(raw_word));
}

static int sbefifo_check_sbe_state(struct sbefifo *sbefifo)
{
	__be32 raw_word;
	u32 sbm;
	int rc;

	rc = fsi_slave_read(sbefifo->fsi_dev->slave, CFAM_GP_MBOX_SBM_ADDR,
			    &raw_word, sizeof(raw_word));
	if (rc)
		return rc;
	sbm = be32_to_cpu(raw_word);

	/* SBE booted at all ? */
	if (!(sbm & CFAM_SBM_SBE_BOOTED))
		return -ESHUTDOWN;

	/* Check its state */
	switch ((sbm & CFAM_SBM_SBE_STATE_MASK) >> CFAM_SBM_SBE_STATE_SHIFT) {
	case SBE_STATE_UNKNOWN:
		return -ESHUTDOWN;
	case SBE_STATE_DMT:
		return -EBUSY;
	case SBE_STATE_IPLING:
	case SBE_STATE_ISTEP:
	case SBE_STATE_MPIPL:
	case SBE_STATE_RUNTIME:
	case SBE_STATE_DUMP: /* Not sure about that one */
		break;
	case SBE_STATE_FAILURE:
	case SBE_STATE_QUIESCE:
		return -ESHUTDOWN;
	}

	/* Is there async FFDC available ? Remember it */
	if (sbm & CFAM_SBM_SBE_ASYNC_FFDC)
		sbefifo->async_ffdc = true;

	return 0;
}

/* Don't flip endianness of data to/from FIFO, just pass through. */
static int sbefifo_down_read(struct sbefifo *sbefifo, __be32 *word)
{
	return fsi_device_read(sbefifo->fsi_dev, SBEFIFO_DOWN, word,
			       sizeof(*word));
}

static int sbefifo_up_write(struct sbefifo *sbefifo, __be32 word)
{
	return fsi_device_write(sbefifo->fsi_dev, SBEFIFO_UP, &word,
				sizeof(word));
}

static int sbefifo_request_reset(struct sbefifo *sbefifo)
{
	struct device *dev = &sbefifo->fsi_dev->dev;
	unsigned long end_time;
	u32 status;
	int rc;

	dev_dbg(dev, "Requesting FIFO reset\n");

	/* Mark broken first, will be cleared if reset succeeds */
	sbefifo->broken = true;

	/* Send reset request */
	rc = sbefifo_regw(sbefifo, SBEFIFO_UP | SBEFIFO_REQ_RESET, 1);
	if (rc) {
		dev_err(dev, "Sending reset request failed, rc=%d\n", rc);
		return rc;
	}

	/* Wait for it to complete */
	end_time = jiffies + msecs_to_jiffies(SBEFIFO_RESET_TIMEOUT);
	while (!time_after(jiffies, end_time)) {
		rc = sbefifo_regr(sbefifo, SBEFIFO_UP | SBEFIFO_STS, &status);
		if (rc) {
			dev_err(dev, "Failed to read UP fifo status during reset"
				" , rc=%d\n", rc);
			return rc;
		}

		if (!(status & SBEFIFO_STS_RESET_REQ)) {
			dev_dbg(dev, "FIFO reset done\n");
			sbefifo->broken = false;
			return 0;
		}

		cond_resched();
	}
	dev_err(dev, "FIFO reset timed out\n");

	return -ETIMEDOUT;
}

static int sbefifo_cleanup_hw(struct sbefifo *sbefifo)
{
	struct device *dev = &sbefifo->fsi_dev->dev;
	u32 up_status, down_status;
	bool need_reset = false;
	int rc;

	rc = sbefifo_check_sbe_state(sbefifo);
	if (rc) {
		dev_dbg(dev, "SBE state=%d\n", rc);
		return rc;
	}

	/* If broken, we don't need to look at status, go straight to reset */
	if (sbefifo->broken)
		goto do_reset;

	rc = sbefifo_regr(sbefifo, SBEFIFO_UP | SBEFIFO_STS, &up_status);
	if (rc) {
		dev_err(dev, "Cleanup: Reading UP status failed, rc=%d\n", rc);

		/* Will try reset again on next attempt at using it */
		sbefifo->broken = true;
		return rc;
	}

	rc = sbefifo_regr(sbefifo, SBEFIFO_DOWN | SBEFIFO_STS, &down_status);
	if (rc) {
		dev_err(dev, "Cleanup: Reading DOWN status failed, rc=%d\n", rc);

		/* Will try reset again on next attempt at using it */
		sbefifo->broken = true;
		return rc;
	}

	/* The FIFO already contains a reset request from the SBE ? */
	if (down_status & SBEFIFO_STS_RESET_REQ) {
		dev_info(dev, "Cleanup: FIFO reset request set, resetting\n");
		rc = sbefifo_regw(sbefifo, SBEFIFO_DOWN, SBEFIFO_PERFORM_RESET);
		if (rc) {
			sbefifo->broken = true;
			dev_err(dev, "Cleanup: Reset reg write failed, rc=%d\n", rc);
			return rc;
		}
		sbefifo->broken = false;
		return 0;
	}

	/* Parity error on either FIFO ? */
	if ((up_status | down_status) & SBEFIFO_STS_PARITY_ERR)
		need_reset = true;

	/* Either FIFO not empty ? */
	if (!((up_status & down_status) & SBEFIFO_STS_EMPTY))
		need_reset = true;

	if (!need_reset)
		return 0;

	dev_info(dev, "Cleanup: FIFO not clean (up=0x%08x down=0x%08x)\n",
		 up_status, down_status);

 do_reset:

	/* Mark broken, will be cleared if/when reset succeeds */
	return sbefifo_request_reset(sbefifo);
}

static int sbefifo_wait(struct sbefifo *sbefifo, bool up,
			u32 *status, unsigned long timeout)
{
	struct device *dev = &sbefifo->fsi_dev->dev;
	unsigned long end_time;
	bool ready = false;
	u32 addr, sts = 0;
	int rc;

	dev_vdbg(dev, "Wait on %s fifo...\n", up ? "up" : "down");

	addr = (up ? SBEFIFO_UP : SBEFIFO_DOWN) | SBEFIFO_STS;

	end_time = jiffies + timeout;
	while (!time_after(jiffies, end_time)) {
		cond_resched();
		rc = sbefifo_regr(sbefifo, addr, &sts);
		if (rc < 0) {
			dev_err(dev, "FSI error %d reading status register\n", rc);
			return rc;
		}
		if (!up && sbefifo_parity_err(sts)) {
			dev_err(dev, "Parity error in DOWN FIFO\n");
			return -ENXIO;
		}
		ready = !(up ? sbefifo_full(sts) : sbefifo_empty(sts));
		if (ready)
			break;
	}
	if (!ready) {
		sysfs_notify(&sbefifo->dev.kobj, NULL, dev_attr_timeout.attr.name);
		sbefifo->timed_out = true;
		dev_err(dev, "%s FIFO Timeout ! status=%08x\n", up ? "UP" : "DOWN", sts);
		return -ETIMEDOUT;
	}
	dev_vdbg(dev, "End of wait status: %08x\n", sts);

	sbefifo->timed_out = false;
	*status = sts;

	return 0;
}

static int sbefifo_send_command(struct sbefifo *sbefifo,
				const __be32 *command, size_t cmd_len)
{
	struct device *dev = &sbefifo->fsi_dev->dev;
	size_t len, chunk, vacant = 0, remaining = cmd_len;
	unsigned long timeout;
	u32 status;
	int rc;

	dev_vdbg(dev, "sending command (%zd words, cmd=%04x)\n",
		 cmd_len, be32_to_cpu(command[1]));

	/* As long as there's something to send */
	timeout = msecs_to_jiffies(SBEFIFO_TIMEOUT_START_CMD);
	while (remaining) {
		/* Wait for room in the FIFO */
		rc = sbefifo_wait(sbefifo, true, &status, timeout);
		if (rc < 0)
			return rc;
		timeout = msecs_to_jiffies(SBEFIFO_TIMEOUT_IN_CMD);

		vacant = sbefifo_vacant(status);
		len = chunk = min(vacant, remaining);

		dev_vdbg(dev, "  status=%08x vacant=%zd chunk=%zd\n",
			 status, vacant, chunk);

		/* Write as much as we can */
		while (len--) {
			rc = sbefifo_up_write(sbefifo, *(command++));
			if (rc) {
				dev_err(dev, "FSI error %d writing UP FIFO\n", rc);
				return rc;
			}
		}
		remaining -= chunk;
		vacant -= chunk;
	}

	/* If there's no room left, wait for some to write EOT */
	if (!vacant) {
		rc = sbefifo_wait(sbefifo, true, &status, timeout);
		if (rc)
			return rc;
	}

	/* Send an EOT */
	rc = sbefifo_regw(sbefifo, SBEFIFO_UP | SBEFIFO_EOT_RAISE, 0);
	if (rc)
		dev_err(dev, "FSI error %d writing EOT\n", rc);
	return rc;
}

static int sbefifo_read_response(struct sbefifo *sbefifo, struct iov_iter *response)
{
	struct device *dev = &sbefifo->fsi_dev->dev;
	u32 status, eot_set;
	unsigned long timeout;
	bool overflow = false;
	__be32 data;
	size_t len;
	int rc;

	dev_vdbg(dev, "reading response, buflen = %zd\n", iov_iter_count(response));

	timeout = msecs_to_jiffies(sbefifo->timeout_start_rsp_ms);
	for (;;) {
		/* Grab FIFO status (this will handle parity errors) */
		rc = sbefifo_wait(sbefifo, false, &status, timeout);
		if (rc < 0)
			return rc;
		timeout = msecs_to_jiffies(SBEFIFO_TIMEOUT_IN_RSP);

		/* Decode status */
		len = sbefifo_populated(status);
		eot_set = sbefifo_eot_set(status);

		dev_vdbg(dev, "  chunk size %zd eot_set=0x%x\n", len, eot_set);

		/* Go through the chunk */
		while(len--) {
			/* Read the data */
			rc = sbefifo_down_read(sbefifo, &data);
			if (rc < 0)
				return rc;

			/* Was it an EOT ? */
			if (eot_set & 0x80) {
				/*
				 * There should be nothing else in the FIFO,
				 * if there is, mark broken, this will force
				 * a reset on next use, but don't fail the
				 * command.
				 */
				if (len) {
					dev_warn(dev, "FIFO read hit"
						 " EOT with still %zd data\n",
						 len);
					sbefifo->broken = true;
				}

				/* We are done */
				rc = sbefifo_regw(sbefifo,
						  SBEFIFO_DOWN | SBEFIFO_EOT_ACK, 0);

				/*
				 * If that write fail, still complete the request but mark
				 * the fifo as broken for subsequent reset (not much else
				 * we can do here).
				 */
				if (rc) {
					dev_err(dev, "FSI error %d ack'ing EOT\n", rc);
					sbefifo->broken = true;
				}

				/* Tell whether we overflowed */
				return overflow ? -EOVERFLOW : 0;
			}

			/* Store it if there is room */
			if (iov_iter_count(response) >= sizeof(__be32)) {
				if (copy_to_iter(&data, sizeof(__be32), response) < sizeof(__be32))
					return -EFAULT;
			} else {
				dev_vdbg(dev, "Response overflowed !\n");

				overflow = true;
			}

			/* Next EOT bit */
			eot_set <<= 1;
		}
	}
	/* Shouldn't happen */
	return -EIO;
}

static int sbefifo_do_command(struct sbefifo *sbefifo,
			      const __be32 *command, size_t cmd_len,
			      struct iov_iter *response)
{
	/* Try sending the command */
	int rc = sbefifo_send_command(sbefifo, command, cmd_len);
	if (rc)
		return rc;

	/* Now, get the response */
	return sbefifo_read_response(sbefifo, response);
}

static void sbefifo_collect_async_ffdc(struct sbefifo *sbefifo)
{
	struct device *dev = &sbefifo->fsi_dev->dev;
        struct iov_iter ffdc_iter;
        struct kvec ffdc_iov;
	__be32 *ffdc;
	size_t ffdc_sz;
	__be32 cmd[2];
	int rc;

	sbefifo->async_ffdc = false;
	ffdc = vmalloc(SBEFIFO_MAX_FFDC_SIZE);
	if (!ffdc) {
		dev_err(dev, "Failed to allocate SBE FFDC buffer\n");
		return;
	}
        ffdc_iov.iov_base = ffdc;
	ffdc_iov.iov_len = SBEFIFO_MAX_FFDC_SIZE;
        iov_iter_kvec(&ffdc_iter, WRITE, &ffdc_iov, 1, SBEFIFO_MAX_FFDC_SIZE);
	cmd[0] = cpu_to_be32(2);
	cmd[1] = cpu_to_be32(SBEFIFO_CMD_GET_SBE_FFDC);
	rc = sbefifo_do_command(sbefifo, cmd, 2, &ffdc_iter);
	if (rc != 0) {
		dev_err(dev, "Error %d retrieving SBE FFDC\n", rc);
		goto bail;
	}
	ffdc_sz = SBEFIFO_MAX_FFDC_SIZE - iov_iter_count(&ffdc_iter);
	ffdc_sz /= sizeof(__be32);
	rc = sbefifo_parse_status(dev, SBEFIFO_CMD_GET_SBE_FFDC, ffdc,
				  ffdc_sz, &ffdc_sz);
	if (rc != 0) {
		dev_err(dev, "Error %d decoding SBE FFDC\n", rc);
		goto bail;
	}
	if (ffdc_sz > 0)
		sbefifo_dump_ffdc(dev, ffdc, ffdc_sz, true);
 bail:
	vfree(ffdc);

}

static int __sbefifo_submit(struct sbefifo *sbefifo,
			    const __be32 *command, size_t cmd_len,
			    struct iov_iter *response)
{
	struct device *dev = &sbefifo->fsi_dev->dev;
	int rc;

	if (sbefifo->dead)
		return -ENODEV;

	if (cmd_len < 2 || be32_to_cpu(command[0]) != cmd_len) {
		dev_vdbg(dev, "Invalid command len %zd (header: %d)\n",
			 cmd_len, be32_to_cpu(command[0]));
		return -EINVAL;
	}

	/* First ensure the HW is in a clean state */
	rc = sbefifo_cleanup_hw(sbefifo);
	if (rc)
		return rc;

	/* Look for async FFDC first if any */
	if (sbefifo->async_ffdc)
		sbefifo_collect_async_ffdc(sbefifo);

	rc = sbefifo_do_command(sbefifo, command, cmd_len, response);
	if (rc != 0 && rc != -EOVERFLOW)
		goto fail;
	return rc;
 fail:
	/*
	 * On failure, attempt a reset. Ignore the result, it will mark
	 * the fifo broken if the reset fails
	 */
        sbefifo_request_reset(sbefifo);

	/* Return original error */
	return rc;
}

/**
 * sbefifo_submit() - Submit and SBE fifo command and receive response
 * @dev: The sbefifo device
 * @command: The raw command data
 * @cmd_len: The command size (in 32-bit words)
 * @response: The output response buffer
 * @resp_len: In: Response buffer size, Out: Response size
 *
 * This will perform the entire operation. If the reponse buffer
 * overflows, returns -EOVERFLOW
 */
int sbefifo_submit(struct device *dev, const __be32 *command, size_t cmd_len,
		   __be32 *response, size_t *resp_len)
{
	struct sbefifo *sbefifo;
        struct iov_iter resp_iter;
        struct kvec resp_iov;
	size_t rbytes;
	int rc;

	if (!dev)
		return -ENODEV;
	sbefifo = dev_get_drvdata(dev);
	if (!sbefifo)
		return -ENODEV;
	if (WARN_ON_ONCE(sbefifo->magic != SBEFIFO_MAGIC))
		return -ENODEV;
	if (!resp_len || !command || !response)
		return -EINVAL;

	/* Prepare iov iterator */
	rbytes = (*resp_len) * sizeof(__be32);
	resp_iov.iov_base = response;
	resp_iov.iov_len = rbytes;
        iov_iter_kvec(&resp_iter, WRITE, &resp_iov, 1, rbytes);

	/* Perform the command */
	rc = mutex_lock_interruptible(&sbefifo->lock);
	if (rc)
		return rc;
	rc = __sbefifo_submit(sbefifo, command, cmd_len, &resp_iter);
	mutex_unlock(&sbefifo->lock);

	/* Extract the response length */
	rbytes -= iov_iter_count(&resp_iter);
	*resp_len = rbytes / sizeof(__be32);

	return rc;
}
EXPORT_SYMBOL_GPL(sbefifo_submit);

/*
 * Char device interface
 */

static void sbefifo_release_command(struct sbefifo_user *user)
{
	if (is_vmalloc_addr(user->pending_cmd))
		vfree(user->pending_cmd);
	user->pending_cmd = NULL;
	user->pending_len = 0;
}

static int sbefifo_user_open(struct inode *inode, struct file *file)
{
	struct sbefifo *sbefifo = container_of(inode->i_cdev, struct sbefifo, cdev);
	struct sbefifo_user *user;

	user = kzalloc(sizeof(struct sbefifo_user), GFP_KERNEL);
	if (!user)
		return -ENOMEM;

	file->private_data = user;
	user->sbefifo = sbefifo;
	user->cmd_page = (void *)__get_free_page(GFP_KERNEL);
	if (!user->cmd_page) {
		kfree(user);
		return -ENOMEM;
	}
	mutex_init(&user->file_lock);
	user->read_timeout_ms = SBEFIFO_TIMEOUT_START_RSP;

	return 0;
}

static ssize_t sbefifo_user_read(struct file *file, char __user *buf,
				 size_t len, loff_t *offset)
{
	struct sbefifo_user *user = file->private_data;
	struct sbefifo *sbefifo;
	struct iov_iter resp_iter;
        struct iovec resp_iov;
	size_t cmd_len;
	int rc;

	if (!user)
		return -EINVAL;
	sbefifo = user->sbefifo;
	if (len & 3)
		return -EINVAL;

	mutex_lock(&user->file_lock);

	/* Cronus relies on -EAGAIN after a short read */
	if (user->pending_len == 0) {
		rc = -EAGAIN;
		goto bail;
	}
	if (user->pending_len < 8) {
		rc = -EINVAL;
		goto bail;
	}
	cmd_len = user->pending_len >> 2;

	/* Prepare iov iterator */
	resp_iov.iov_base = buf;
	resp_iov.iov_len = len;
	iov_iter_init(&resp_iter, WRITE, &resp_iov, 1, len);

	/* Perform the command */
	rc = mutex_lock_interruptible(&sbefifo->lock);
	if (rc)
		goto bail;
	sbefifo->timeout_start_rsp_ms = user->read_timeout_ms;
	rc = __sbefifo_submit(sbefifo, user->pending_cmd, cmd_len, &resp_iter);
	sbefifo->timeout_start_rsp_ms = SBEFIFO_TIMEOUT_START_RSP;
	mutex_unlock(&sbefifo->lock);
	if (rc < 0)
		goto bail;

	/* Extract the response length */
	rc = len - iov_iter_count(&resp_iter);
 bail:
	sbefifo_release_command(user);
	mutex_unlock(&user->file_lock);
	return rc;
}

static ssize_t sbefifo_user_write(struct file *file, const char __user *buf,
				  size_t len, loff_t *offset)
{
	struct sbefifo_user *user = file->private_data;
	struct sbefifo *sbefifo;
	int rc = len;

	if (!user)
		return -EINVAL;
	sbefifo = user->sbefifo;
	if (len > SBEFIFO_MAX_USER_CMD_LEN)
		return -EINVAL;
	if (len & 3)
		return -EINVAL;

	mutex_lock(&user->file_lock);

	/* Can we use the pre-allocate buffer ? If not, allocate */
	if (len <= PAGE_SIZE)
		user->pending_cmd = user->cmd_page;
	else
		user->pending_cmd = vmalloc(len);
	if (!user->pending_cmd) {
		rc = -ENOMEM;
		goto bail;
	}

	/* Copy the command into the staging buffer */
	if (copy_from_user(user->pending_cmd, buf, len)) {
		rc = -EFAULT;
		goto bail;
	}

	/* Check for the magic reset command */
	if (len == 4 && be32_to_cpu(*(__be32 *)user->pending_cmd) ==
	    SBEFIFO_RESET_MAGIC)  {

		/* Clear out any pending command */
		user->pending_len = 0;

		/* Trigger reset request */
		rc = mutex_lock_interruptible(&sbefifo->lock);
		if (rc)
			goto bail;
		rc = sbefifo_request_reset(user->sbefifo);
		mutex_unlock(&sbefifo->lock);
		if (rc == 0)
			rc = 4;
		goto bail;
	}

	/* Update the staging buffer size */
	user->pending_len = len;
 bail:
	if (!user->pending_len)
		sbefifo_release_command(user);

	mutex_unlock(&user->file_lock);

	/* And that's it, we'll issue the command on a read */
	return rc;
}

static int sbefifo_user_release(struct inode *inode, struct file *file)
{
	struct sbefifo_user *user = file->private_data;

	if (!user)
		return -EINVAL;

	sbefifo_release_command(user);
	free_page((unsigned long)user->cmd_page);
	kfree(user);

	return 0;
}

static int sbefifo_read_timeout(struct sbefifo_user *user, void __user *argp)
{
	struct device *dev = &user->sbefifo->dev;
	u32 timeout;

	if (get_user(timeout, (__u32 __user *)argp))
		return -EFAULT;

	if (timeout == 0) {
		user->read_timeout_ms = SBEFIFO_TIMEOUT_START_RSP;
		dev_dbg(dev, "Timeout reset to %d\n", user->read_timeout_ms);
		return 0;
	}

	if (timeout < 10 || timeout > 120)
		return -EINVAL;

	user->read_timeout_ms = timeout * 1000; /* user timeout is in sec */

	dev_dbg(dev, "Timeout set to %d\n", user->read_timeout_ms);

	return 0;
}

static long sbefifo_user_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sbefifo_user *user = file->private_data;
	int rc = -ENOTTY;

	if (!user)
		return -EINVAL;

	mutex_lock(&user->file_lock);
	switch (cmd) {
	case FSI_SBEFIFO_READ_TIMEOUT_SECONDS:
		rc = sbefifo_read_timeout(user, (void __user *)arg);
		break;
	}
	mutex_unlock(&user->file_lock);
	return rc;
}

static const struct file_operations sbefifo_fops = {
	.owner		= THIS_MODULE,
	.open		= sbefifo_user_open,
	.read		= sbefifo_user_read,
	.write		= sbefifo_user_write,
	.release	= sbefifo_user_release,
	.unlocked_ioctl = sbefifo_user_ioctl,
};

static void sbefifo_free(struct device *dev)
{
	struct sbefifo *sbefifo = container_of(dev, struct sbefifo, dev);

	put_device(&sbefifo->fsi_dev->dev);
	kfree(sbefifo);
}

/*
 * Probe/remove
 */

static int sbefifo_probe(struct device *dev)
{
	struct fsi_device *fsi_dev = to_fsi_dev(dev);
	struct sbefifo *sbefifo;
	struct device_node *np;
	struct platform_device *child;
	char child_name[32];
	int rc, didx, child_idx = 0;

	dev_dbg(dev, "Found sbefifo device\n");

	sbefifo = kzalloc(sizeof(*sbefifo), GFP_KERNEL);
	if (!sbefifo)
		return -ENOMEM;

	/* Grab a reference to the device (parent of our cdev), we'll drop it later */
	if (!get_device(dev)) {
		kfree(sbefifo);
		return -ENODEV;
	}

	sbefifo->magic = SBEFIFO_MAGIC;
	sbefifo->fsi_dev = fsi_dev;
	dev_set_drvdata(dev, sbefifo);
	mutex_init(&sbefifo->lock);
	sbefifo->timeout_start_rsp_ms = SBEFIFO_TIMEOUT_START_RSP;

	/*
	 * Try cleaning up the FIFO. If this fails, we still register the
	 * driver and will try cleaning things up again on the next access.
	 */
	rc = sbefifo_cleanup_hw(sbefifo);
	if (rc && rc != -ESHUTDOWN)
		dev_err(dev, "Initial HW cleanup failed, will retry later\n");

	/* Create chardev for userspace access */
	sbefifo->dev.type = &fsi_cdev_type;
	sbefifo->dev.parent = dev;
	sbefifo->dev.release = sbefifo_free;
	device_initialize(&sbefifo->dev);

	/* Allocate a minor in the FSI space */
	rc = fsi_get_new_minor(fsi_dev, fsi_dev_sbefifo, &sbefifo->dev.devt, &didx);
	if (rc)
		goto err;

	dev_set_name(&sbefifo->dev, "sbefifo%d", didx);
	cdev_init(&sbefifo->cdev, &sbefifo_fops);
	rc = cdev_device_add(&sbefifo->cdev, &sbefifo->dev);
	if (rc) {
		dev_err(dev, "Error %d creating char device %s\n",
			rc, dev_name(&sbefifo->dev));
		goto err_free_minor;
	}

	/* Create platform devs for dts child nodes (occ, etc) */
	for_each_available_child_of_node(dev->of_node, np) {
		snprintf(child_name, sizeof(child_name), "%s-dev%d",
			 dev_name(&sbefifo->dev), child_idx++);
		child = of_platform_device_create(np, child_name, dev);
		if (!child)
			dev_warn(dev, "failed to create child %s dev\n",
				 child_name);
	}

	device_create_file(&sbefifo->dev, &dev_attr_timeout);

	return 0;
 err_free_minor:
	fsi_free_minor(sbefifo->dev.devt);
 err:
	put_device(&sbefifo->dev);
	return rc;
}

static int sbefifo_unregister_child(struct device *dev, void *data)
{
	struct platform_device *child = to_platform_device(dev);

	of_device_unregister(child);
	if (dev->of_node)
		of_node_clear_flag(dev->of_node, OF_POPULATED);

	return 0;
}

static int sbefifo_remove(struct device *dev)
{
	struct sbefifo *sbefifo = dev_get_drvdata(dev);

	dev_dbg(dev, "Removing sbefifo device...\n");

	device_remove_file(&sbefifo->dev, &dev_attr_timeout);

	mutex_lock(&sbefifo->lock);
	sbefifo->dead = true;
	mutex_unlock(&sbefifo->lock);

	cdev_device_del(&sbefifo->cdev, &sbefifo->dev);
	fsi_free_minor(sbefifo->dev.devt);
	device_for_each_child(dev, NULL, sbefifo_unregister_child);
	put_device(&sbefifo->dev);

	return 0;
}

static const struct fsi_device_id sbefifo_ids[] = {
	{
		.engine_type = FSI_ENGID_SBE,
		.version = FSI_VERSION_ANY,
	},
	{ 0 }
};

static struct fsi_driver sbefifo_drv = {
	.id_table = sbefifo_ids,
	.drv = {
		.name = DEVICE_NAME,
		.bus = &fsi_bus_type,
		.probe = sbefifo_probe,
		.remove = sbefifo_remove,
	}
};

static int sbefifo_init(void)
{
	return fsi_driver_register(&sbefifo_drv);
}

static void sbefifo_exit(void)
{
	fsi_driver_unregister(&sbefifo_drv);
}

module_init(sbefifo_init);
module_exit(sbefifo_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brad Bishop <bradleyb@fuzziesquirrel.com>");
MODULE_AUTHOR("Eddie James <eajames@linux.vnet.ibm.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Linux device interface to the POWER Self Boot Engine");
