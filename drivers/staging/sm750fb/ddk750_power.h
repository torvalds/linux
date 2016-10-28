#ifndef DDK750_POWER_H__
#define DDK750_POWER_H__

typedef enum _DPMS_t {
	crtDPMS_ON = 0x0,
	crtDPMS_STANDBY = 0x1,
	crtDPMS_SUSPEND = 0x2,
	crtDPMS_OFF = 0x3,
}
DPMS_t;

#define setDAC(off) {							\
	POKE32(MISC_CTRL,						\
	       (PEEK32(MISC_CTRL) & ~MISC_CTRL_DAC_POWER_OFF) | (off)); \
}

void ddk750_set_dpms(DPMS_t);
void set_power_mode(unsigned int powerMode);
void set_current_gate(unsigned int gate);

/*
 * This function enable/disable the 2D engine.
 */
void enable_2d_engine(unsigned int enable);

/*
 * This function enable/disable the DMA Engine
 */
void enable_dma(unsigned int enable);

/*
 * This function enable/disable the GPIO Engine
 */
void enable_gpio(unsigned int enable);

/*
 * This function enable/disable the I2C Engine
 */
void enable_i2c(unsigned int enable);


#endif
