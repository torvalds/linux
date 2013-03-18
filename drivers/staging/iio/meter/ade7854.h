#ifndef _ADE7854_H
#define _ADE7854_H

#define ADE7854_AIGAIN    0x4380
#define ADE7854_AVGAIN    0x4381
#define ADE7854_BIGAIN    0x4382
#define ADE7854_BVGAIN    0x4383
#define ADE7854_CIGAIN    0x4384
#define ADE7854_CVGAIN    0x4385
#define ADE7854_NIGAIN    0x4386
#define ADE7854_AIRMSOS   0x4387
#define ADE7854_AVRMSOS   0x4388
#define ADE7854_BIRMSOS   0x4389
#define ADE7854_BVRMSOS   0x438A
#define ADE7854_CIRMSOS   0x438B
#define ADE7854_CVRMSOS   0x438C
#define ADE7854_NIRMSOS   0x438D
#define ADE7854_AVAGAIN   0x438E
#define ADE7854_BVAGAIN   0x438F
#define ADE7854_CVAGAIN   0x4390
#define ADE7854_AWGAIN    0x4391
#define ADE7854_AWATTOS   0x4392
#define ADE7854_BWGAIN    0x4393
#define ADE7854_BWATTOS   0x4394
#define ADE7854_CWGAIN    0x4395
#define ADE7854_CWATTOS   0x4396
#define ADE7854_AVARGAIN  0x4397
#define ADE7854_AVAROS    0x4398
#define ADE7854_BVARGAIN  0x4399
#define ADE7854_BVAROS    0x439A
#define ADE7854_CVARGAIN  0x439B
#define ADE7854_CVAROS    0x439C
#define ADE7854_AFWGAIN   0x439D
#define ADE7854_AFWATTOS  0x439E
#define ADE7854_BFWGAIN   0x439F
#define ADE7854_BFWATTOS  0x43A0
#define ADE7854_CFWGAIN   0x43A1
#define ADE7854_CFWATTOS  0x43A2
#define ADE7854_AFVARGAIN 0x43A3
#define ADE7854_AFVAROS   0x43A4
#define ADE7854_BFVARGAIN 0x43A5
#define ADE7854_BFVAROS   0x43A6
#define ADE7854_CFVARGAIN 0x43A7
#define ADE7854_CFVAROS   0x43A8
#define ADE7854_VATHR1    0x43A9
#define ADE7854_VATHR0    0x43AA
#define ADE7854_WTHR1     0x43AB
#define ADE7854_WTHR0     0x43AC
#define ADE7854_VARTHR1   0x43AD
#define ADE7854_VARTHR0   0x43AE
#define ADE7854_RSV       0x43AF
#define ADE7854_VANOLOAD  0x43B0
#define ADE7854_APNOLOAD  0x43B1
#define ADE7854_VARNOLOAD 0x43B2
#define ADE7854_VLEVEL    0x43B3
#define ADE7854_DICOEFF   0x43B5
#define ADE7854_HPFDIS    0x43B6
#define ADE7854_ISUMLVL   0x43B8
#define ADE7854_ISUM      0x43BF
#define ADE7854_AIRMS     0x43C0
#define ADE7854_AVRMS     0x43C1
#define ADE7854_BIRMS     0x43C2
#define ADE7854_BVRMS     0x43C3
#define ADE7854_CIRMS     0x43C4
#define ADE7854_CVRMS     0x43C5
#define ADE7854_NIRMS     0x43C6
#define ADE7854_RUN       0xE228
#define ADE7854_AWATTHR   0xE400
#define ADE7854_BWATTHR   0xE401
#define ADE7854_CWATTHR   0xE402
#define ADE7854_AFWATTHR  0xE403
#define ADE7854_BFWATTHR  0xE404
#define ADE7854_CFWATTHR  0xE405
#define ADE7854_AVARHR    0xE406
#define ADE7854_BVARHR    0xE407
#define ADE7854_CVARHR    0xE408
#define ADE7854_AFVARHR   0xE409
#define ADE7854_BFVARHR   0xE40A
#define ADE7854_CFVARHR   0xE40B
#define ADE7854_AVAHR     0xE40C
#define ADE7854_BVAHR     0xE40D
#define ADE7854_CVAHR     0xE40E
#define ADE7854_IPEAK     0xE500
#define ADE7854_VPEAK     0xE501
#define ADE7854_STATUS0   0xE502
#define ADE7854_STATUS1   0xE503
#define ADE7854_OILVL     0xE507
#define ADE7854_OVLVL     0xE508
#define ADE7854_SAGLVL    0xE509
#define ADE7854_MASK0     0xE50A
#define ADE7854_MASK1     0xE50B
#define ADE7854_IAWV      0xE50C
#define ADE7854_IBWV      0xE50D
#define ADE7854_ICWV      0xE50E
#define ADE7854_VAWV      0xE510
#define ADE7854_VBWV      0xE511
#define ADE7854_VCWV      0xE512
#define ADE7854_AWATT     0xE513
#define ADE7854_BWATT     0xE514
#define ADE7854_CWATT     0xE515
#define ADE7854_AVA       0xE519
#define ADE7854_BVA       0xE51A
#define ADE7854_CVA       0xE51B
#define ADE7854_CHECKSUM  0xE51F
#define ADE7854_VNOM      0xE520
#define ADE7854_PHSTATUS  0xE600
#define ADE7854_ANGLE0    0xE601
#define ADE7854_ANGLE1    0xE602
#define ADE7854_ANGLE2    0xE603
#define ADE7854_PERIOD    0xE607
#define ADE7854_PHNOLOAD  0xE608
#define ADE7854_LINECYC   0xE60C
#define ADE7854_ZXTOUT    0xE60D
#define ADE7854_COMPMODE  0xE60E
#define ADE7854_GAIN      0xE60F
#define ADE7854_CFMODE    0xE610
#define ADE7854_CF1DEN    0xE611
#define ADE7854_CF2DEN    0xE612
#define ADE7854_CF3DEN    0xE613
#define ADE7854_APHCAL    0xE614
#define ADE7854_BPHCAL    0xE615
#define ADE7854_CPHCAL    0xE616
#define ADE7854_PHSIGN    0xE617
#define ADE7854_CONFIG    0xE618
#define ADE7854_MMODE     0xE700
#define ADE7854_ACCMODE   0xE701
#define ADE7854_LCYCMODE  0xE702
#define ADE7854_PEAKCYC   0xE703
#define ADE7854_SAGCYC    0xE704
#define ADE7854_CFCYC     0xE705
#define ADE7854_HSDC_CFG  0xE706
#define ADE7854_CONFIG2   0xEC01

#define ADE7854_READ_REG   0x1
#define ADE7854_WRITE_REG  0x0

#define ADE7854_MAX_TX    7
#define ADE7854_MAX_RX    7
#define ADE7854_STARTUP_DELAY 1

#define ADE7854_SPI_SLOW	(u32)(300 * 1000)
#define ADE7854_SPI_BURST	(u32)(1000 * 1000)
#define ADE7854_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct ade7854_state - device instance specific data
 * @spi:			actual spi_device
 * @indio_dev:		industrial I/O device structure
 * @buf_lock:		mutex to protect tx and rx
 * @tx:			transmit buffer
 * @rx:			receive buffer
 **/
struct ade7854_state {
	struct spi_device	*spi;
	struct i2c_client	*i2c;
	int			(*read_reg_8) (struct device *, u16, u8 *);
	int			(*read_reg_16) (struct device *, u16, u16 *);
	int			(*read_reg_24) (struct device *, u16, u32 *);
	int			(*read_reg_32) (struct device *, u16, u32 *);
	int			(*write_reg_8) (struct device *, u16, u8);
	int			(*write_reg_16) (struct device *, u16, u16);
	int			(*write_reg_24) (struct device *, u16, u32);
	int			(*write_reg_32) (struct device *, u16, u32);
	int			irq;
	struct mutex		buf_lock;
	u8			tx[ADE7854_MAX_TX] ____cacheline_aligned;
	u8			rx[ADE7854_MAX_RX];

};

extern int ade7854_probe(struct iio_dev *indio_dev, struct device *dev);
extern int ade7854_remove(struct iio_dev *indio_dev);

#endif
