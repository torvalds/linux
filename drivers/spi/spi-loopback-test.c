/*
 *  linux/drivers/spi/spi-loopback-test.c
 *
 *  (c) Martin Sperl <kernel@martin.sperl.org>
 *
 *  Loopback test driver to test several typical spi_message conditions
 *  that a spi_master driver may encounter
 *  this can also get used for regression testing
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>

#include "spi-test.h"

/* flag to only simulate transfers */
int simulate_only;
module_param(simulate_only, int, 0);
MODULE_PARM_DESC(simulate_only, "if not 0 do not execute the spi message");

/* dump spi messages */
int dump_messages;
module_param(dump_messages, int, 0);
MODULE_PARM_DESC(dump_messages,
		 "=1 dump the basic spi_message_structure, " \
		 "=2 dump the spi_message_structure including data, " \
		 "=3 dump the spi_message structure before and after execution");
/* the device is jumpered for loopback - enabling some rx_buf tests */
int loopback;
module_param(loopback, int, 0);
MODULE_PARM_DESC(loopback,
		 "if set enable loopback mode, where the rx_buf "	\
		 "is checked to match tx_buf after the spi_message "	\
		 "is executed");

/* run only a specific test */
int run_only_test = -1;
module_param(run_only_test, int, 0);
MODULE_PARM_DESC(run_only_test,
		 "only run the test with this number (0-based !)");

/* the actual tests to execute */
static struct spi_test spi_tests[] = {
	{
		.description	= "tx/rx-transfer - start of page",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_rx_align = ITERATE_ALIGN,
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
				.rx_buf = RX(0),
			},
		},
	},
	{
		.description	= "tx/rx-transfer - crossing PAGE_SIZE",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_rx_align = ITERATE_ALIGN,
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(PAGE_SIZE - 4),
				.rx_buf = RX(PAGE_SIZE - 4),
			},
		},
	},
	{
		.description	= "tx-transfer - only",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
			},
		},
	},
	{
		.description	= "rx-transfer - only",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_rx_align = ITERATE_ALIGN,
		.transfers		= {
			{
				.len = 1,
				.rx_buf = RX(0),
			},
		},
	},
	{
		.description	= "two tx-transfers - alter both",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(0) | BIT(1),
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
			},
			{
				.len = 1,
				/* this is why we cant use ITERATE_MAX_LEN */
				.tx_buf = TX(SPI_TEST_MAX_SIZE_HALF),
			},
		},
	},
	{
		.description	= "two tx-transfers - alter first",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(1),
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(64),
			},
			{
				.len = 1,
				.tx_buf = TX(0),
			},
		},
	},
	{
		.description	= "two tx-transfers - alter second",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(0),
		.transfers		= {
			{
				.len = 16,
				.tx_buf = TX(0),
			},
			{
				.len = 1,
				.tx_buf = TX(64),
			},
		},
	},
	{
		.description	= "two transfers tx then rx - alter both",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(0) | BIT(1),
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
			},
			{
				.len = 1,
				.rx_buf = RX(0),
			},
		},
	},
	{
		.description	= "two transfers tx then rx - alter tx",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(0),
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
			},
			{
				.len = 1,
				.rx_buf = RX(0),
			},
		},
	},
	{
		.description	= "two transfers tx then rx - alter rx",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(1),
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
			},
			{
				.len = 1,
				.rx_buf = RX(0),
			},
		},
	},
	{
		.description	= "two tx+rx transfers - alter both",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(0) | BIT(1),
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
				.rx_buf = RX(0),
			},
			{
				.len = 1,
				/* making sure we align without overwrite
				 * the reason we can not use ITERATE_MAX_LEN
				 */
				.tx_buf = TX(SPI_TEST_MAX_SIZE_HALF),
				.rx_buf = RX(SPI_TEST_MAX_SIZE_HALF),
			},
		},
	},
	{
		.description	= "two tx+rx transfers - alter first",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(0),
		.transfers		= {
			{
				.len = 1,
				/* making sure we align without overwrite */
				.tx_buf = TX(1024),
				.rx_buf = RX(1024),
			},
			{
				.len = 1,
				/* making sure we align without overwrite */
				.tx_buf = TX(0),
				.rx_buf = RX(0),
			},
		},
	},
	{
		.description	= "two tx+rx transfers - alter second",
		.fill_option	= FILL_COUNT_8,
		.iterate_len    = { ITERATE_MAX_LEN },
		.iterate_tx_align = ITERATE_ALIGN,
		.iterate_transfer_mask = BIT(1),
		.transfers		= {
			{
				.len = 1,
				.tx_buf = TX(0),
				.rx_buf = RX(0),
			},
			{
				.len = 1,
				/* making sure we align without overwrite */
				.tx_buf = TX(1024),
				.rx_buf = RX(1024),
			},
		},
	},

	{ /* end of tests sequence */ }
};

static int spi_loopback_test_probe(struct spi_device *spi)
{
	int ret;

	dev_info(&spi->dev, "Executing spi-loopback-tests\n");

	ret = spi_test_run_tests(spi, spi_tests);

	dev_info(&spi->dev, "Finished spi-loopback-tests with return: %i\n",
		 ret);

	return ret;
}

/* non const match table to permit to change via a module parameter */
static struct of_device_id spi_loopback_test_of_match[] = {
	{ .compatible	= "linux,spi-loopback-test", },
	{ }
};

/* allow to override the compatible string via a module_parameter */
module_param_string(compatible, spi_loopback_test_of_match[0].compatible,
		    sizeof(spi_loopback_test_of_match[0].compatible),
		    0000);

MODULE_DEVICE_TABLE(of, spi_loopback_test_of_match);

static struct spi_driver spi_loopback_test_driver = {
	.driver = {
		.name = "spi-loopback-test",
		.owner = THIS_MODULE,
		.of_match_table = spi_loopback_test_of_match,
	},
	.probe = spi_loopback_test_probe,
};

module_spi_driver(spi_loopback_test_driver);

MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_DESCRIPTION("test spi_driver to check core functionality");
MODULE_LICENSE("GPL");

/*-------------------------------------------------------------------------*/

/* spi_test implementation */

#define RANGE_CHECK(ptr, plen, start, slen) \
	((ptr >= start) && (ptr + plen <= start + slen))

/* we allocate one page more, to allow for offsets */
#define SPI_TEST_MAX_SIZE_PLUS (SPI_TEST_MAX_SIZE + PAGE_SIZE)

static void spi_test_print_hex_dump(char *pre, const void *ptr, size_t len)
{
	/* limit the hex_dump */
	if (len < 1024) {
		print_hex_dump(KERN_INFO, pre,
			       DUMP_PREFIX_OFFSET, 16, 1,
			       ptr, len, 0);
		return;
	}
	/* print head */
	print_hex_dump(KERN_INFO, pre,
		       DUMP_PREFIX_OFFSET, 16, 1,
		       ptr, 512, 0);
	/* print tail */
	pr_info("%s truncated - continuing at offset %04zx\n",
		pre, len - 512);
	print_hex_dump(KERN_INFO, pre,
		       DUMP_PREFIX_OFFSET, 16, 1,
		       ptr + (len - 512), 512, 0);
}

static void spi_test_dump_message(struct spi_device *spi,
				  struct spi_message *msg,
				  bool dump_data)
{
	struct spi_transfer *xfer;
	int i;
	u8 b;

	dev_info(&spi->dev, "  spi_msg@%pK\n", msg);
	if (msg->status)
		dev_info(&spi->dev, "    status:        %i\n",
			 msg->status);
	dev_info(&spi->dev, "    frame_length:  %i\n",
		 msg->frame_length);
	dev_info(&spi->dev, "    actual_length: %i\n",
		 msg->actual_length);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		dev_info(&spi->dev, "    spi_transfer@%pK\n", xfer);
		dev_info(&spi->dev, "      len:    %i\n", xfer->len);
		dev_info(&spi->dev, "      tx_buf: %pK\n", xfer->tx_buf);
		if (dump_data && xfer->tx_buf)
			spi_test_print_hex_dump("          TX: ",
						xfer->tx_buf,
						xfer->len);

		dev_info(&spi->dev, "      rx_buf: %pK\n", xfer->rx_buf);
		if (dump_data && xfer->rx_buf)
			spi_test_print_hex_dump("          RX: ",
						xfer->rx_buf,
						xfer->len);
		/* check for unwritten test pattern on rx_buf */
		if (xfer->rx_buf) {
			for (i = 0 ; i < xfer->len ; i++) {
				b = ((u8 *)xfer->rx_buf)[xfer->len - 1 - i];
				if (b != SPI_TEST_PATTERN_UNWRITTEN)
					break;
			}
			if (i)
				dev_info(&spi->dev,
					 "      rx_buf filled with %02x starts at offset: %i\n",
					 SPI_TEST_PATTERN_UNWRITTEN,
					 xfer->len - i);
		}
	}
}

struct rx_ranges {
	struct list_head list;
	u8 *start;
	u8 *end;
};

static int rx_ranges_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct rx_ranges *rx_a = list_entry(a, struct rx_ranges, list);
	struct rx_ranges *rx_b = list_entry(b, struct rx_ranges, list);

	if (rx_a->start > rx_b->start)
		return 1;
	if (rx_a->start < rx_b->start)
		return -1;
	return 0;
}

static int spi_check_rx_ranges(struct spi_device *spi,
			       struct spi_message *msg,
			       void *rx)
{
	struct spi_transfer *xfer;
	struct rx_ranges ranges[SPI_TEST_MAX_TRANSFERS], *r;
	int i = 0;
	LIST_HEAD(ranges_list);
	u8 *addr;
	int ret = 0;

	/* loop over all transfers to fill in the rx_ranges */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* if there is no rx, then no check is needed */
		if (!xfer->rx_buf)
			continue;
		/* fill in the rx_range */
		if (RANGE_CHECK(xfer->rx_buf, xfer->len,
				rx, SPI_TEST_MAX_SIZE_PLUS)) {
			ranges[i].start = xfer->rx_buf;
			ranges[i].end = xfer->rx_buf + xfer->len;
			list_add(&ranges[i].list, &ranges_list);
			i++;
		}
	}

	/* if no ranges, then we can return and avoid the checks...*/
	if (!i)
		return 0;

	/* sort the list */
	list_sort(NULL, &ranges_list, rx_ranges_cmp);

	/* and iterate over all the rx addresses */
	for (addr = rx; addr < (u8 *)rx + SPI_TEST_MAX_SIZE_PLUS; addr++) {
		/* if we are the DO not write pattern,
		 * then continue with the loop...
		 */
		if (*addr == SPI_TEST_PATTERN_DO_NOT_WRITE)
			continue;

		/* check if we are inside a range */
		list_for_each_entry(r, &ranges_list, list) {
			/* if so then set to end... */
			if ((addr >= r->start) && (addr < r->end))
				addr = r->end;
		}
		/* second test after a (hopefull) translation */
		if (*addr == SPI_TEST_PATTERN_DO_NOT_WRITE)
			continue;

		/* if still not found then something has modified too much */
		/* we could list the "closest" transfer here... */
		dev_err(&spi->dev,
			"loopback strangeness - rx changed outside of allowed range at: %pK\n",
			addr);
		/* do not return, only set ret,
		 * so that we list all addresses
		 */
		ret = -ERANGE;
	}

	return ret;
}

static int spi_test_check_loopback_result(struct spi_device *spi,
					  struct spi_message *msg,
					  void *tx, void *rx)
{
	struct spi_transfer *xfer;
	u8 rxb, txb;
	size_t i;
	int ret;

	/* checks rx_buffer pattern are valid with loopback or without */
	ret = spi_check_rx_ranges(spi, msg, rx);
	if (ret)
		return ret;

	/* if we run without loopback, then return now */
	if (!loopback)
		return 0;

	/* if applicable to transfer check that rx_buf is equal to tx_buf */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* if there is no rx, then no check is needed */
		if (!xfer->rx_buf)
			continue;
		/* so depending on tx_buf we need to handle things */
		if (xfer->tx_buf) {
			for (i = 1; i < xfer->len; i++) {
				txb = ((u8 *)xfer->tx_buf)[i];
				rxb = ((u8 *)xfer->rx_buf)[i];
				if (txb != rxb)
					goto mismatch_error;
			}
		} else {
			/* first byte received */
			txb = ((u8 *)xfer->rx_buf)[0];
			/* first byte may be 0 or xff */
			if (!((txb == 0) || (txb == 0xff))) {
				dev_err(&spi->dev,
					"loopback strangeness - we expect 0x00 or 0xff, but not 0x%02x\n",
					txb);
				return -EINVAL;
			}
			/* check that all bytes are identical */
			for (i = 1; i < xfer->len; i++) {
				rxb = ((u8 *)xfer->rx_buf)[i];
				if (rxb != txb)
					goto mismatch_error;
			}
		}
	}

	return 0;

mismatch_error:
	dev_err(&spi->dev,
		"loopback strangeness - transfer mismatch on byte %04zx - expected 0x%02x, but got 0x%02x\n",
		i, txb, rxb);

	return -EINVAL;
}

static int spi_test_translate(struct spi_device *spi,
			      void **ptr, size_t len,
			      void *tx, void *rx)
{
	size_t off;

	/* return on null */
	if (!*ptr)
		return 0;

	/* in the MAX_SIZE_HALF case modify the pointer */
	if (((size_t)*ptr) & SPI_TEST_MAX_SIZE_HALF)
		/* move the pointer to the correct range */
		*ptr += (SPI_TEST_MAX_SIZE_PLUS / 2) -
			SPI_TEST_MAX_SIZE_HALF;

	/* RX range
	 * - we check against MAX_SIZE_PLUS to allow for automated alignment
	 */
	if (RANGE_CHECK(*ptr, len,  RX(0), SPI_TEST_MAX_SIZE_PLUS)) {
		off = *ptr - RX(0);
		*ptr = rx + off;

		return 0;
	}

	/* TX range */
	if (RANGE_CHECK(*ptr, len,  TX(0), SPI_TEST_MAX_SIZE_PLUS)) {
		off = *ptr - TX(0);
		*ptr = tx + off;

		return 0;
	}

	dev_err(&spi->dev,
		"PointerRange [%pK:%pK[ not in range [%pK:%pK[ or [%pK:%pK[\n",
		*ptr, *ptr + len,
		RX(0), RX(SPI_TEST_MAX_SIZE),
		TX(0), TX(SPI_TEST_MAX_SIZE));

	return -EINVAL;
}

static int spi_test_fill_pattern(struct spi_device *spi,
				 struct spi_test *test)
{
	struct spi_transfer *xfers = test->transfers;
	u8 *tx_buf;
	size_t count = 0;
	int i, j;

#ifdef __BIG_ENDIAN
#define GET_VALUE_BYTE(value, index, bytes) \
	(value >> (8 * (bytes - 1 - count % bytes)))
#else
#define GET_VALUE_BYTE(value, index, bytes) \
	(value >> (8 * (count % bytes)))
#endif

	/* fill all transfers with the pattern requested */
	for (i = 0; i < test->transfer_count; i++) {
		/* fill rx_buf with SPI_TEST_PATTERN_UNWRITTEN */
		if (xfers[i].rx_buf)
			memset(xfers[i].rx_buf, SPI_TEST_PATTERN_UNWRITTEN,
			       xfers[i].len);
		/* if tx_buf is NULL then skip */
		tx_buf = (u8 *)xfers[i].tx_buf;
		if (!tx_buf)
			continue;
		/* modify all the transfers */
		for (j = 0; j < xfers[i].len; j++, tx_buf++, count++) {
			/* fill tx */
			switch (test->fill_option) {
			case FILL_MEMSET_8:
				*tx_buf = test->fill_pattern;
				break;
			case FILL_MEMSET_16:
				*tx_buf = GET_VALUE_BYTE(test->fill_pattern,
							 count, 2);
				break;
			case FILL_MEMSET_24:
				*tx_buf = GET_VALUE_BYTE(test->fill_pattern,
							 count, 3);
				break;
			case FILL_MEMSET_32:
				*tx_buf = GET_VALUE_BYTE(test->fill_pattern,
							 count, 4);
				break;
			case FILL_COUNT_8:
				*tx_buf = count;
				break;
			case FILL_COUNT_16:
				*tx_buf = GET_VALUE_BYTE(count, count, 2);
				break;
			case FILL_COUNT_24:
				*tx_buf = GET_VALUE_BYTE(count, count, 3);
				break;
			case FILL_COUNT_32:
				*tx_buf = GET_VALUE_BYTE(count, count, 4);
				break;
			case FILL_TRANSFER_BYTE_8:
				*tx_buf = j;
				break;
			case FILL_TRANSFER_BYTE_16:
				*tx_buf = GET_VALUE_BYTE(j, j, 2);
				break;
			case FILL_TRANSFER_BYTE_24:
				*tx_buf = GET_VALUE_BYTE(j, j, 3);
				break;
			case FILL_TRANSFER_BYTE_32:
				*tx_buf = GET_VALUE_BYTE(j, j, 4);
				break;
			case FILL_TRANSFER_NUM:
				*tx_buf = i;
				break;
			default:
				dev_err(&spi->dev,
					"unsupported fill_option: %i\n",
					test->fill_option);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int _spi_test_run_iter(struct spi_device *spi,
			      struct spi_test *test,
			      void *tx, void *rx)
{
	struct spi_message *msg = &test->msg;
	struct spi_transfer *x;
	int i, ret;

	/* initialize message - zero-filled via static initialization */
	spi_message_init_no_memset(msg);

	/* fill rx with the DO_NOT_WRITE pattern */
	memset(rx, SPI_TEST_PATTERN_DO_NOT_WRITE, SPI_TEST_MAX_SIZE_PLUS);

	/* add the individual transfers */
	for (i = 0; i < test->transfer_count; i++) {
		x = &test->transfers[i];

		/* patch the values of tx_buf */
		ret = spi_test_translate(spi, (void **)&x->tx_buf, x->len,
					 (void *)tx, rx);
		if (ret)
			return ret;

		/* patch the values of rx_buf */
		ret = spi_test_translate(spi, &x->rx_buf, x->len,
					 (void *)tx, rx);
		if (ret)
			return ret;

		/* and add it to the list */
		spi_message_add_tail(x, msg);
	}

	/* fill in the transfer buffers with pattern */
	ret = spi_test_fill_pattern(spi, test);
	if (ret)
		return ret;

	/* and execute */
	if (test->execute_msg)
		ret = test->execute_msg(spi, test, tx, rx);
	else
		ret = spi_test_execute_msg(spi, test, tx, rx);

	/* handle result */
	if (ret == test->expected_return)
		return 0;

	dev_err(&spi->dev,
		"test failed - test returned %i, but we expect %i\n",
		ret, test->expected_return);

	if (ret)
		return ret;

	/* if it is 0, as we expected something else,
	 * then return something special
	 */
	return -EFAULT;
}

static int spi_test_run_iter(struct spi_device *spi,
			     const struct spi_test *testtemplate,
			     void *tx, void *rx,
			     size_t len,
			     size_t tx_off,
			     size_t rx_off
	)
{
	struct spi_test test;
	int i, tx_count, rx_count;

	/* copy the test template to test */
	memcpy(&test, testtemplate, sizeof(test));

	/* set up test->transfers to the correct count */
	if (!test.transfer_count) {
		for (i = 0;
		    (i < SPI_TEST_MAX_TRANSFERS) && test.transfers[i].len;
		    i++) {
			test.transfer_count++;
		}
	}

	/* if iterate_transfer_mask is not set,
	 * then set it to first transfer only
	 */
	if (!(test.iterate_transfer_mask & (BIT(test.transfer_count) - 1)))
		test.iterate_transfer_mask = 1;

	/* count number of transfers with tx/rx_buf != NULL */
	rx_count = tx_count = 0;
	for (i = 0; i < test.transfer_count; i++) {
		if (test.transfers[i].tx_buf)
			tx_count++;
		if (test.transfers[i].rx_buf)
			rx_count++;
	}

	/* in some iteration cases warn and exit early,
	 * as there is nothing to do, that has not been tested already...
	 */
	if (tx_off && (!tx_count)) {
		dev_warn_once(&spi->dev,
			      "%s: iterate_tx_off configured with tx_buf==NULL - ignoring\n",
			      test.description);
		return 0;
	}
	if (rx_off && (!rx_count)) {
		dev_warn_once(&spi->dev,
			      "%s: iterate_rx_off configured with rx_buf==NULL - ignoring\n",
			      test.description);
		return 0;
	}

	/* write out info */
	if (!(len || tx_off || rx_off)) {
		dev_info(&spi->dev, "Running test %s\n", test.description);
	} else {
		dev_info(&spi->dev,
			 "  with iteration values: len = %zu, tx_off = %zu, rx_off = %zu\n",
			 len, tx_off, rx_off);
	}

	/* update in the values from iteration values */
	for (i = 0; i < test.transfer_count; i++) {
		/* only when bit in transfer mask is set */
		if (!(test.iterate_transfer_mask & BIT(i)))
			continue;
		if (len)
			test.transfers[i].len = len;
		if (test.transfers[i].tx_buf)
			test.transfers[i].tx_buf += tx_off;
		if (test.transfers[i].tx_buf)
			test.transfers[i].rx_buf += rx_off;
	}

	/* and execute */
	return _spi_test_run_iter(spi, &test, tx, rx);
}

/**
 * spi_test_execute_msg - default implementation to run a test
 *
 * spi: @spi_device on which to run the @spi_message
 * test: the test to execute, which already contains @msg
 * tx:   the tx buffer allocated for the test sequence
 * rx:   the rx buffer allocated for the test sequence
 *
 * Returns: error code of spi_sync as well as basic error checking
 */
int spi_test_execute_msg(struct spi_device *spi, struct spi_test *test,
			 void *tx, void *rx)
{
	struct spi_message *msg = &test->msg;
	int ret = 0;
	int i;

	/* only if we do not simulate */
	if (!simulate_only) {
		/* dump the complete message before and after the transfer */
		if (dump_messages == 3)
			spi_test_dump_message(spi, msg, true);

		/* run spi message */
		ret = spi_sync(spi, msg);
		if (ret == -ETIMEDOUT) {
			dev_info(&spi->dev,
				 "spi-message timed out - reruning...\n");
			/* rerun after a few explicit schedules */
			for (i = 0; i < 16; i++)
				schedule();
			ret = spi_sync(spi, msg);
		}
		if (ret) {
			dev_err(&spi->dev,
				"Failed to execute spi_message: %i\n",
				ret);
			goto exit;
		}

		/* do some extra error checks */
		if (msg->frame_length != msg->actual_length) {
			dev_err(&spi->dev,
				"actual length differs from expected\n");
			ret = -EIO;
			goto exit;
		}

		/* run rx-buffer tests */
		ret = spi_test_check_loopback_result(spi, msg, tx, rx);
	}

	/* if requested or on error dump message (including data) */
exit:
	if (dump_messages || ret)
		spi_test_dump_message(spi, msg,
				      (dump_messages >= 2) || (ret));

	return ret;
}
EXPORT_SYMBOL_GPL(spi_test_execute_msg);

/**
 * spi_test_run_test - run an individual spi_test
 *                     including all the relevant iterations on:
 *                     length and buffer alignment
 *
 * spi:  the spi_device to send the messages to
 * test: the test which we need to execute
 * tx:   the tx buffer allocated for the test sequence
 * rx:   the rx buffer allocated for the test sequence
 *
 * Returns: status code of spi_sync or other failures
 */

int spi_test_run_test(struct spi_device *spi, const struct spi_test *test,
		      void *tx, void *rx)
{
	int idx_len;
	size_t len;
	size_t tx_align, rx_align;
	int ret;

	/* test for transfer limits */
	if (test->transfer_count >= SPI_TEST_MAX_TRANSFERS) {
		dev_err(&spi->dev,
			"%s: Exceeded max number of transfers with %i\n",
			test->description, test->transfer_count);
		return -E2BIG;
	}

	/* setting up some values in spi_message
	 * based on some settings in spi_master
	 * some of this can also get done in the run() method
	 */

	/* iterate over all the iterable values using macros
	 * (to make it a bit more readable...
	 */
#define FOR_EACH_ITERATE(var, defaultvalue)				\
	for (idx_##var = -1, var = defaultvalue;			\
	     ((idx_##var < 0) ||					\
		     (							\
			     (idx_##var < SPI_TEST_MAX_ITERATE) &&	\
			     (var = test->iterate_##var[idx_##var])	\
		     )							\
	     );								\
	     idx_##var++)
#define FOR_EACH_ALIGNMENT(var)						\
	for (var = 0;							\
	    var < (test->iterate_##var ?				\
			(spi->master->dma_alignment ?			\
			 spi->master->dma_alignment :			\
			 test->iterate_##var) :				\
			1);						\
	    var++)

	FOR_EACH_ITERATE(len, 0) {
		FOR_EACH_ALIGNMENT(tx_align) {
			FOR_EACH_ALIGNMENT(rx_align) {
				/* and run the iteration */
				ret = spi_test_run_iter(spi, test,
							tx, rx,
							len,
							tx_align,
							rx_align);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(spi_test_run_test);

/**
 * spi_test_run_tests - run an array of spi_messages tests
 * @spi: the spi device on which to run the tests
 * @tests: NULL-terminated array of @spi_test
 *
 * Returns: status errors as per @spi_test_run_test()
 */

int spi_test_run_tests(struct spi_device *spi,
		       struct spi_test *tests)
{
	char *rx = NULL, *tx = NULL;
	int ret = 0, count = 0;
	struct spi_test *test;

	/* allocate rx/tx buffers of 128kB size without devm
	 * in the hope that is on a page boundary
	 */
	rx = kzalloc(SPI_TEST_MAX_SIZE_PLUS, GFP_KERNEL);
	if (!rx) {
		ret = -ENOMEM;
		goto out;
	}

	tx = kzalloc(SPI_TEST_MAX_SIZE_PLUS, GFP_KERNEL);
	if (!tx) {
		ret = -ENOMEM;
		goto out;
	}

	/* now run the individual tests in the table */
	for (test = tests, count = 0; test->description[0];
	     test++, count++) {
		/* only run test if requested */
		if ((run_only_test > -1) && (count != run_only_test))
			continue;
		/* run custom implementation */
		if (test->run_test)
			ret = test->run_test(spi, test, tx, rx);
		else
			ret = spi_test_run_test(spi, test, tx, rx);
		if (ret)
			goto out;
		/* add some delays so that we can easily
		 * detect the individual tests when using a logic analyzer
		 * we also add scheduling to avoid potential spi_timeouts...
		 */
		mdelay(100);
		schedule();
	}

out:
	kfree(rx);
	kfree(tx);
	return ret;
}
EXPORT_SYMBOL_GPL(spi_test_run_tests);
