#ifndef PM_VOL_H
#define PM_VOL_H


#if defined(CONFIG_RK29_SPI_INSRAM)||defined(CONFIG_RK29_PWM_INSRAM)||defined(CONFIG_RK29_I2C_INSRAM)

void interface_ctr_reg_pread(void);
void i2c_interface_ctr_reg_pread(void);
unsigned int __sramfunc rk29_suspend_voltage_set(unsigned int vol);
void __sramfunc rk29_suspend_voltage_resume(unsigned int vol);

#else

#define interface_ctr_reg_pread()
#define i2c_interface_ctr_reg_pread()
static unsigned int __sramfunc rk29_suspend_voltage_set(unsigned int vol) { return 0; }
#define rk29_suspend_voltage_resume(a)

#endif

#endif
