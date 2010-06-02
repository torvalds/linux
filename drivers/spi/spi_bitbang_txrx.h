/*
 * Mix this utility code with some glue code to get one of several types of
 * simple SPI master driver.  Two do polled word-at-a-time I/O:
 *
 *   -	GPIO/parport bitbangers.  Provide chipselect() and txrx_word[](),
 *	expanding the per-word routines from the inline templates below.
 *
 *   -	Drivers for controllers resembling bare shift registers.  Provide
 *	chipselect() and txrx_word[](), with custom setup()/cleanup() methods
 *	that use your controller's clock and chipselect registers.
 *
 * Some hardware works well with requests at spi_transfer scope:
 *
 *   -	Drivers leveraging smarter hardware, with fifos or DMA; or for half
 *	duplex (MicroWire) controllers.  Provide chipselect() and txrx_bufs(),
 *	and custom setup()/cleanup() methods.
 */

/*
 * The code that knows what GPIO pins do what should have declared four
 * functions, ideally as inlines, before including this header:
 *
 *  void setsck(struct spi_device *, int is_on);
 *  void setmosi(struct spi_device *, int is_on);
 *  int getmiso(struct spi_device *);
 *  void spidelay(unsigned);
 *
 * setsck()'s is_on parameter is a zero/nonzero boolean.
 *
 * setmosi()'s is_on parameter is a zero/nonzero boolean.
 *
 * getmiso() is required to return 0 or 1 only. Any other value is invalid
 * and will result in improper operation.
 *
 * A non-inlined routine would call bitbang_txrx_*() routines.  The
 * main loop could easily compile down to a handful of instructions,
 * especially if the delay is a NOP (to run at peak speed).
 *
 * Since this is software, the timings may not be exactly what your board's
 * chips need ... there may be several reasons you'd need to tweak timings
 * in these routines, not just make to make it faster or slower to match a
 * particular CPU clock rate.
 */

static inline u32
bitbang_txrx_be_cpha0(struct spi_device *spi,
		unsigned nsecs, unsigned cpol,
		u32 word, u8 bits)
{
	/* if (cpol == 0) this is SPI_MODE_0; else this is SPI_MODE_2 */

	/* clock starts at inactive polarity */
	for (word <<= (32 - bits); likely(bits); bits--) {

		/* setup MSB (to slave) on trailing edge */
		setmosi(spi, word & (1 << 31));
		spidelay(nsecs);	/* T(setup) */

		setsck(spi, !cpol);
		spidelay(nsecs);

		/* sample MSB (from slave) on leading edge */
		word <<= 1;
		word |= getmiso(spi);
		setsck(spi, cpol);
	}
	return word;
}

static inline u32
bitbang_txrx_be_cpha1(struct spi_device *spi,
		unsigned nsecs, unsigned cpol,
		u32 word, u8 bits)
{
	/* if (cpol == 0) this is SPI_MODE_1; else this is SPI_MODE_3 */

	/* clock starts at inactive polarity */
	for (word <<= (32 - bits); likely(bits); bits--) {

		/* setup MSB (to slave) on leading edge */
		setsck(spi, !cpol);
		setmosi(spi, word & (1 << 31));
		spidelay(nsecs); /* T(setup) */

		setsck(spi, cpol);
		spidelay(nsecs);

		/* sample MSB (from slave) on trailing edge */
		word <<= 1;
		word |= getmiso(spi);
	}
	return word;
}
