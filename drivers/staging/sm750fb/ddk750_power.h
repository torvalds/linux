#ifndef DDK750_POWER_H__
#define DDK750_POWER_H__

typedef enum _DPMS_t {
	crtDPMS_ON = 0x0,
	crtDPMS_STANDBY = 0x1,
	crtDPMS_SUSPEND = 0x2,
	crtDPMS_OFF = 0x3,
}
DPMS_t;

#define setDAC(off) \
		{	\
		POKE32(MISC_CTRL, FIELD_VALUE(PEEK32(MISC_CTRL), \
									MISC_CTRL,	\
									DAC_POWER,	\
									off));	\
		}

void ddk750_setDPMS(DPMS_t);

unsigned int getPowerMode(void);

/*
 * This function sets the current power mode
 */
void setPowerMode(unsigned int powerMode);

/*
 * This function sets current gate
 */
void setCurrentGate(unsigned int gate);

/*
 * This function enable/disable the 2D engine.
 */
void enable2DEngine(unsigned int enable);

/*
 * This function enable/disable the ZV Port
 */
void enableZVPort(unsigned int enable);

/*
 * This function enable/disable the DMA Engine
 */
void enableDMA(unsigned int enable);

/*
 * This function enable/disable the GPIO Engine
 */
void enableGPIO(unsigned int enable);

/*
 * This function enable/disable the PWM Engine
 */
void enablePWM(unsigned int enable);

/*
 * This function enable/disable the I2C Engine
 */
void enableI2C(unsigned int enable);

/*
 * This function enable/disable the SSP.
 */
void enableSSP(unsigned int enable);


#endif
