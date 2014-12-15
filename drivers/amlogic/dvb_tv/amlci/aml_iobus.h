
#ifndef _AML_IOBUS_
#define _AML_IOBUS_

int aml_iobus_init(void);
int aml_iobus_exit(void);

unsigned int aml_iobus_io_read(unsigned char addr);
int aml_iobus_io_write(unsigned char addr, unsigned char val);

unsigned int aml_iobus_attr_read(unsigned int addr);
int aml_iobus_attr_write(unsigned int addr, unsigned char val);

#endif/*_AML_IOBUS_*/

