#ifndef W83977AF_H
#define W83977AF_H

#define W977_EFIO_BASE 0x370
#define W977_EFIO2_BASE 0x3f0
#define W977_DEVICE_IR 0x06


/*
 * Enter extended function mode
 */
static inline void w977_efm_enter(unsigned int efio)
{
        outb(0x87, efio);
        outb(0x87, efio);
}

/*
 * Select a device to configure 
 */

static inline void w977_select_device(__u8 devnum, unsigned int efio)
{
	outb(0x07, efio);
	outb(devnum, efio+1);
} 

/* 
 * Write a byte to a register
 */
static inline void w977_write_reg(__u8 reg, __u8 value, unsigned int efio)
{
	outb(reg, efio);
	outb(value, efio+1);
}

/*
 * read a byte from a register
 */
static inline __u8 w977_read_reg(__u8 reg, unsigned int efio)
{
	outb(reg, efio);
	return inb(efio+1);
}

/*
 * Exit extended function mode
 */
static inline void w977_efm_exit(unsigned int efio)
{
	outb(0xAA, efio);
}
#endif
