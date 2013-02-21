#ifndef _ADE7754_H
#define _ADE7754_H

#define ADE7754_AENERGY   0x01
#define ADE7754_RAENERGY  0x02
#define ADE7754_LAENERGY  0x03
#define ADE7754_VAENERGY  0x04
#define ADE7754_RVAENERGY 0x05
#define ADE7754_LVAENERGY 0x06
#define ADE7754_PERIOD    0x07
#define ADE7754_TEMP      0x08
#define ADE7754_WFORM     0x09
#define ADE7754_OPMODE    0x0A
#define ADE7754_MMODE     0x0B
#define ADE7754_WAVMODE   0x0C
#define ADE7754_WATMODE   0x0D
#define ADE7754_VAMODE    0x0E
#define ADE7754_IRQEN     0x0F
#define ADE7754_STATUS    0x10
#define ADE7754_RSTATUS   0x11
#define ADE7754_ZXTOUT    0x12
#define ADE7754_LINCYC    0x13
#define ADE7754_SAGCYC    0x14
#define ADE7754_SAGLVL    0x15
#define ADE7754_VPEAK     0x16
#define ADE7754_IPEAK     0x17
#define ADE7754_GAIN      0x18
#define ADE7754_AWG       0x19
#define ADE7754_BWG       0x1A
#define ADE7754_CWG       0x1B
#define ADE7754_AVAG      0x1C
#define ADE7754_BVAG      0x1D
#define ADE7754_CVAG      0x1E
#define ADE7754_APHCAL    0x1F
#define ADE7754_BPHCAL    0x20
#define ADE7754_CPHCAL    0x21
#define ADE7754_AAPOS     0x22
#define ADE7754_BAPOS     0x23
#define ADE7754_CAPOS     0x24
#define ADE7754_CFNUM     0x25
#define ADE7754_CFDEN     0x26
#define ADE7754_WDIV      0x27
#define ADE7754_VADIV     0x28
#define ADE7754_AIRMS     0x29
#define ADE7754_BIRMS     0x2A
#define ADE7754_CIRMS     0x2B
#define ADE7754_AVRMS     0x2C
#define ADE7754_BVRMS     0x2D
#define ADE7754_CVRMS     0x2E
#define ADE7754_AIRMSOS   0x2F
#define ADE7754_BIRMSOS   0x30
#define ADE7754_CIRMSOS   0x31
#define ADE7754_AVRMSOS   0x32
#define ADE7754_BVRMSOS   0x33
#define ADE7754_CVRMSOS   0x34
#define ADE7754_AAPGAIN   0x35
#define ADE7754_BAPGAIN   0x36
#define ADE7754_CAPGAIN   0x37
#define ADE7754_AVGAIN    0x38
#define ADE7754_BVGAIN    0x39
#define ADE7754_CVGAIN    0x3A
#define ADE7754_CHKSUM    0x3E
#define ADE7754_VERSION   0x3F

#define ADE7754_READ_REG(a)    a
#define ADE7754_WRITE_REG(a) ((a) | 0x80)

#define ADE7754_MAX_TX    4
#define ADE7754_MAX_RX    4
#define ADE7754_STARTUP_DELAY 1

#define ADE7754_SPI_SLOW	(u32)(300 * 1000)
#define ADE7754_SPI_BURST	(u32)(1000 * 1000)
#define ADE7754_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct ade7754_state - device instance specific data
 * @us:			actual spi_device
 * @buf_lock:		mutex to protect tx and rx
 * @tx:			transmit buffer
 * @rx:			receive buffer
 **/
struct ade7754_state {
	struct spi_device	*us;
	struct mutex		buf_lock;
	u8			tx[ADE7754_MAX_TX] ____cacheline_aligned;
	u8			rx[ADE7754_MAX_RX];
};

#endif
