#ifndef _IIO_DUMMY_EVGEN_H_
#define _IIO_DUMMY_EVGEN_H_

struct iio_dummy_regs {
	u32 reg_id;
	u32 reg_data;
};

struct iio_dummy_regs *iio_dummy_evgen_get_regs(int irq);
int iio_dummy_evgen_get_irq(void);
void iio_dummy_evgen_release_irq(int irq);

#endif /* _IIO_DUMMY_EVGEN_H_ */
