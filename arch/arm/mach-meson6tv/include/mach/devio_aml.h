#ifndef _DEVIO_AML_H_
#define _DEVIO_AML_H_

struct devio_aml_platform_data {
	int (*io_setup)(void*);
	int (*io_cleanup)(void*);
	int (*io_power)(void *, int enable);
	int (*io_reset)(void *, int enable);
};

#endif/*define _DEVIO_AML_H_*/

