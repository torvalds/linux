/*
 *  linux/drivers/spi/spi-test.h
 *
 *  (c) Martin Sperl <kernel@martin.sperl.org>
 *
 *  spi_test definitions
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

#include <linux/spi/spi.h>

#define SPI_TEST_MAX_TRANSFERS 4
#define SPI_TEST_MAX_SIZE (32 * PAGE_SIZE)
#define SPI_TEST_MAX_ITERATE 32

/* the "dummy" start addresses used in spi_test
 * these addresses get translated at a later stage
 */
#define RX_START	BIT(30)
#define TX_START	BIT(31)
#define RX(off)		((void *)(RX_START + off))
#define TX(off)		((void *)(TX_START + off))

/* some special defines for offsets */
#define SPI_TEST_MAX_SIZE_HALF	BIT(29)

/* detection pattern for unfinished reads...
 * - 0x00 or 0xff could be valid levels for tx_buf = NULL,
 * so we do not use either of them
 */
#define SPI_TEST_PATTERN_UNWRITTEN 0xAA
#define SPI_TEST_PATTERN_DO_NOT_WRITE 0x55
#define SPI_TEST_CHECK_DO_NOT_WRITE 64

/**
 * struct spi_test - describes a specific (set of) tests to execute
 *
 * @description:      description of the test
 *
 * @msg:              a template @spi_message usedfor the default settings
 * @transfers:        array of @spi_transfers that are part of the
 *                    resulting spi_message. The first transfer with len == 0
 *                    signifies the end of the list
 * @transfer_count:   normally computed number of transfers with len > 0
 *
 * @run_test:         run a specific spi_test - this allows to override
 *                    the default implementation of @spi_test_run_transfer
 *                    either to add some custom filters for a specific test
 *                    or to effectively run some very custom tests...
 * @execute_msg:      run the spi_message for real - this allows to override
 *                    @spi_test_execute_msg to apply final modifications
 *                    on the spi_message
 * @expected_return:  the expected return code - in some cases we want to
 *                    test also for error conditions
 *
 * @iterate_len:      list of length to iterate on (in addition to the
 *                    explicitly set @spi_transfer.len)
 * @iterate_tx_align: change the alignment of @spi_transfer.tx_buf
 *                    for all values in the below range if set.
 *                    the ranges are:
 *                    [0 : @spi_master.dma_alignment[ if set
 *                    [0 : iterate_tx_align[ if unset
 * @iterate_rx_align: change the alignment of @spi_transfer.rx_buf
 *                    see @iterate_tx_align for details
 * @iterate_transfer_mask: the bitmask of transfers to which the iterations
 *                         apply - if 0, then it applies to all transfer
 *
 * @fill_option:      define the way how tx_buf is filled
 * @fill_pattern:     fill pattern to apply to the tx_buf
 *                    (used in some of the @fill_options)
 */

struct spi_test {
	char description[64];
	struct spi_message msg;
	struct spi_transfer transfers[SPI_TEST_MAX_TRANSFERS];
	unsigned int transfer_count;
	int (*run_test)(struct spi_device *spi, struct spi_test *test,
			void *tx, void *rx);
	int (*execute_msg)(struct spi_device *spi, struct spi_test *test,
			   void *tx, void *rx);
	int expected_return;
	/* iterate over all the non-zero values */
	int iterate_len[SPI_TEST_MAX_ITERATE];
	int iterate_tx_align;
	int iterate_rx_align;
	u32 iterate_transfer_mask;
	/* the tx-fill operation */
	u32 fill_option;
#define FILL_MEMSET_8	0	/* just memset with 8 bit */
#define FILL_MEMSET_16	1	/* just memset with 16 bit */
#define FILL_MEMSET_24	2	/* just memset with 24 bit */
#define FILL_MEMSET_32	3	/* just memset with 32 bit */
#define FILL_COUNT_8	4	/* fill with a 8 byte counter */
#define FILL_COUNT_16	5	/* fill with a 16 bit counter */
#define FILL_COUNT_24	6	/* fill with a 24 bit counter */
#define FILL_COUNT_32	7	/* fill with a 32 bit counter */
#define FILL_TRANSFER_BYTE_8  8	/* fill with the transfer byte - 8 bit */
#define FILL_TRANSFER_BYTE_16 9	/* fill with the transfer byte - 16 bit */
#define FILL_TRANSFER_BYTE_24 10 /* fill with the transfer byte - 24 bit */
#define FILL_TRANSFER_BYTE_32 11 /* fill with the transfer byte - 32 bit */
#define FILL_TRANSFER_NUM     16 /* fill with the transfer number */
	u32 fill_pattern;
};

/* default implementation for @spi_test.run_test */
int spi_test_run_test(struct spi_device *spi,
		      const struct spi_test *test,
		      void *tx, void *rx);

/* default implementation for @spi_test.execute_msg */
int spi_test_execute_msg(struct spi_device *spi,
			 struct spi_test *test,
			 void *tx, void *rx);

/* function to execute a set of tests */
int spi_test_run_tests(struct spi_device *spi,
		       struct spi_test *tests);

/* some of the default @spi_transfer.len to test */
#define ITERATE_LEN 2, 3, 7, 11, 16, 31, 32, 64, 97, 128, 251, 256, \
		1021, 1024, 1031, 4093, PAGE_SIZE, 4099, 65536, 65537

#define ITERATE_MAX_LEN ITERATE_LEN, SPI_TEST_MAX_SIZE - 1, SPI_TEST_MAX_SIZE

/* the default alignment to test */
#define ITERATE_ALIGN sizeof(int)
