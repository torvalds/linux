/* ssp.h
     $Id$

   written by Marc Singer
   6 Dec 2004

   Copyright (C) 2004 Marc Singer

   -----------
   DESCRIPTION
   -----------

   This SSP header is available throughout the kernel, for this
   machine/architecture, because drivers that use it may be dispersed.

   This file was cloned from the 7952x implementation.  It would be
   better to share them, but we're taking an easier approach for the
   time being.

*/

#if !defined (__SSP_H__)
#    define   __SSP_H__

/* ----- Includes */

/* ----- Types */

struct ssp_driver {
	int  (*init)		(void);
	void (*exit)		(void);
	void (*acquire)		(void);
	void (*release)		(void);
	int  (*configure)	(int device, int mode, int speed,
				 int frame_size_write, int frame_size_read);
	void (*chip_select)	(int enable);
	void (*set_callbacks)   (void* handle,
				 irqreturn_t (*callback_tx)(void*),
				 irqreturn_t (*callback_rx)(void*));
	void (*enable)		(void);
	void (*disable)		(void);
//	int  (*save_state)	(void*);
//	void (*restore_state)	(void*);
	int  (*read)		(void);
	int  (*write)		(u16 data);
	int  (*write_read)	(u16 data);
	void (*flush)		(void);
	void (*write_async)	(void* pv, size_t cb);
	size_t (*write_pos)	(void);
};

	/* These modes are only available on the LH79524 */
#define SSP_MODE_SPI		(1)
#define SSP_MODE_SSI		(2)
#define SSP_MODE_MICROWIRE	(3)
#define SSP_MODE_I2S		(4)

	/* CPLD SPI devices */
#define DEVICE_EEPROM	0	/* Configuration eeprom */
#define DEVICE_MAC	1	/* MAC eeprom (LPD79524) */
#define DEVICE_CODEC	2	/* Audio codec */
#define DEVICE_TOUCH	3	/* Touch screen (LPD79520) */

/* ----- Globals */

/* ----- Prototypes */

//extern struct ssp_driver lh79520_i2s_driver;
extern struct ssp_driver lh7a400_cpld_ssp_driver;

#endif  /* __SSP_H__ */
