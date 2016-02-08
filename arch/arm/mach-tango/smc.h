extern int tango_smc(unsigned int val, unsigned int service);

#define tango_set_l2_control(val)	tango_smc(val, 0x102)
#define tango_start_aux_core(val)	tango_smc(val, 0x104)
#define tango_set_aux_boot_addr(val)	tango_smc((unsigned int)val, 0x105)
