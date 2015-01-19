/*
 * aml_pio_i2c.h
 */
 
#ifndef AML_PIO_I2C_H
#define AML_PIO_I2C_H
#include <linux/i2c-aml.h>

#define AML_PIO_OUTPUT		1
#define AML_GPIO_INPUT		0

struct aml_sw_i2c {
	struct aml_sw_i2c_pins 		*sw_pins;
	struct i2c_adapter 			adapter;	
	struct i2c_algo_bit_data 	algo_data;
	struct class                class;
};

#endif


