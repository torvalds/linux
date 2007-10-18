#ifndef _LINUX_VFC_H_
#define _LINUX_VFC_H_

/*
 * The control register for the vfc is at offset 0x4000
 * The first field ram bank is located at offset 0x5000
 * The second field ram bank is at offset 0x7000
 * i2c_reg address the Phillips PCF8584(see notes in vfc_i2c.c) 
 *    data and transmit register.
 * i2c_s1 controls register s1 of the PCF8584
 * i2c_write seems to be similar to i2c_write but I am not 
 *    quite sure why sun uses it
 * 
 * I am also not sure whether or not you can read the fram bank as a
 * whole or whether you must read each word individually from offset
 * 0x5000 as soon as I figure it out I will update this file */

struct vfc_regs {
	char pad1[0x4000];
	unsigned int control;  /* Offset 0x4000 */
	char pad2[0xffb];      /* from offset 0x4004 to 0x5000 */
	unsigned int fram_bank1; /* Offset 0x5000 */
	char pad3[0xffb];        /* from offset 0x5004 to 0x6000 */
	unsigned int i2c_reg; /* Offset 0x6000 */
	unsigned int i2c_magic2; /* Offset 0x6004 */
	unsigned int i2c_s1;  /* Offset 0x6008 */
	unsigned int i2c_write; /* Offset 0x600c */
	char pad4[0xff0];     /* from offset 0x6010 to 0x7000 */
	unsigned int fram_bank2; /* Offset 0x7000 */
	char pad5[0x1000];
};

#define VFC_SAA9051_NR (13)
#define VFC_SAA9051_ADDR (0x8a)
	/* The saa9051 returns the following for its status 
	 * bit 0 - 0
	 * bit 1 - SECAM color detected (1=found,0=not found)
	 * bit 2 - COLOR detected (1=found,0=not found)
	 * bit 3 - 0
	 * bit 4 - Field frequency bit (1=60Hz (NTSC), 0=50Hz (PAL))
	 * bit 5 - 1
	 * bit 6 - horizontal frequency lock (1=transmitter found,
	 *                                    0=no transmitter)
	 * bit 7 - Power on reset bit (1=reset,0=at least one successful 
	 *                                       read of the status byte)
	 */

#define VFC_SAA9051_PONRES (0x80)
#define VFC_SAA9051_HLOCK (0x40)
#define VFC_SAA9051_FD (0x10)
#define VFC_SAA9051_CD (0x04)
#define VFC_SAA9051_CS (0x02)


/* The various saa9051 sub addresses */

#define VFC_SAA9051_IDEL (0) 
#define VFC_SAA9051_HSY_START (1)
#define VFC_SAA9051_HSY_STOP (2)
#define VFC_SAA9051_HC_START (3)
#define VFC_SAA9051_HC_STOP (4)
#define VFC_SAA9051_HS_START (5)
#define VFC_SAA9051_HORIZ_PEAK (6)
#define VFC_SAA9051_HUE (7)
#define VFC_SAA9051_C1 (8)
#define VFC_SAA9051_C2 (9)
#define VFC_SAA9051_C3 (0xa)
#define VFC_SAA9051_SECAM_DELAY (0xb)


/* Bit settings for saa9051 sub address 0x06 */

#define VFC_SAA9051_AP1 (0x01)
#define VFC_SAA9051_AP2 (0x02)
#define VFC_SAA9051_COR1 (0x04)
#define VFC_SAA9051_COR2 (0x08)
#define VFC_SAA9051_BP1 (0x10)
#define VFC_SAA9051_BP2 (0x20)
#define VFC_SAA9051_PF (0x40)
#define VFC_SAA9051_BY (0x80)


/* Bit settings for saa9051 sub address 0x08 */

#define VFC_SAA9051_CCFR0 (0x01)
#define VFC_SAA9051_CCFR1 (0x02)
#define VFC_SAA9051_YPN (0x04)
#define VFC_SAA9051_ALT (0x08)
#define VFC_SAA9051_CO (0x10)
#define VFC_SAA9051_VTR (0x20)
#define VFC_SAA9051_FS (0x40)
#define VFC_SAA9051_HPLL (0x80)


/* Bit settings for saa9051 sub address 9 */

#define VFC_SAA9051_SS0 (0x01)
#define VFC_SAA9051_SS1 (0x02)
#define VFC_SAA9051_AFCC (0x04)
#define VFC_SAA9051_CI (0x08)
#define VFC_SAA9051_SA9D4 (0x10) /* Don't care bit */
#define VFC_SAA9051_OEC (0x20)
#define VFC_SAA9051_OEY (0x40)
#define VFC_SAA9051_VNL (0x80)


/* Bit settings for saa9051 sub address 0x0A */

#define VFC_SAA9051_YDL0 (0x01)
#define VFC_SAA9051_YDL1 (0x02)
#define VFC_SAA9051_YDL2 (0x04)
#define VFC_SAA9051_SS2 (0x08)
#define VFC_SAA9051_SS3 (0x10)
#define VFC_SAA9051_YC (0x20)
#define VFC_SAA9051_CT (0x40)
#define VFC_SAA9051_SYC (0x80)


#define VFC_SAA9051_SA(a,b) ((a)->saa9051_state_array[(b)+1])
#define vfc_update_saa9051(a) (vfc_i2c_sendbuf((a),VFC_SAA9051_ADDR,\
					    (a)->saa9051_state_array,\
					    VFC_SAA9051_NR))


struct vfc_dev {
	volatile struct vfc_regs __iomem *regs;
	struct vfc_regs *phys_regs;
	unsigned int control_reg;
	struct mutex device_lock_mtx;
	int instance;
	int busy;
	unsigned long which_io;
	unsigned char saa9051_state_array[VFC_SAA9051_NR];
};

extern struct vfc_dev **vfc_dev_lst;

void captstat_reset(struct vfc_dev *);
void memptr_reset(struct vfc_dev *);

int vfc_pcf8584_init(struct vfc_dev *);
void vfc_i2c_delay_no_busy(struct vfc_dev *, unsigned long);
void vfc_i2c_delay(struct vfc_dev *);
int vfc_i2c_sendbuf(struct vfc_dev *, unsigned char, char *, int) ;
int vfc_i2c_recvbuf(struct vfc_dev *, unsigned char, char *, int) ;
int vfc_i2c_reset_bus(struct vfc_dev *);
int vfc_init_i2c_bus(struct vfc_dev *);
void vfc_lock_device(struct vfc_dev *);
void vfc_unlock_device(struct vfc_dev *);

#define VFC_CONTROL_DIAGMODE  0x10000000
#define VFC_CONTROL_MEMPTR    0x20000000
#define VFC_CONTROL_CAPTURE   0x02000000
#define VFC_CONTROL_CAPTRESET 0x04000000

#define VFC_STATUS_CAPTURE    0x08000000

#ifdef VFC_IOCTL_DEBUG
#define VFC_IOCTL_DEBUG_PRINTK(a) printk a
#else
#define VFC_IOCTL_DEBUG_PRINTK(a)
#endif

#ifdef VFC_I2C_DEBUG
#define VFC_I2C_DEBUG_PRINTK(a) printk a
#else
#define VFC_I2C_DEBUG_PRINTK(a)
#endif

#endif /* _LINUX_VFC_H_ */





